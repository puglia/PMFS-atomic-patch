/*
 * BRIEF DESCRIPTION
 *
 * File operations for files.
 *
 * Copyright 2012-2013 Intel Corporation
 * Copyright 2009-2011 Marco Stornelli <marco.stornelli@gmail.com>
 * Copyright 2003 Sony Corporation
 * Copyright 2003 Matsushita Electric Industrial Co., Ltd.
 * 2003-2004 (c) MontaVista Software, Inc. , Steve Longerbeam
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uio.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/falloc.h>
#include <linux/pagevec.h>
#include <linux/writeback.h>
#include <asm/mman.h>
#include "pmfs.h"
#include "xip.h"
#include "pcm_i.h"

static inline int pmfs_can_set_blocksize_hint(struct pmfs_inode *pi,
					       loff_t new_size)
{
	/* Currently, we don't deallocate data blocks till the file is deleted.
	 * So no changing blocksize hints once allocation is done. */
	if (le64_to_cpu(pi->root))
		return 0;
	return 1;
}

int pmfs_set_blocksize_hint(struct super_block *sb, struct pmfs_inode *pi,
		loff_t new_size)
{
	unsigned short block_type;

	if (!pmfs_can_set_blocksize_hint(pi, new_size))
		return 0;

	if (new_size >= 0x40000000) {   /* 1G */
		block_type = PMFS_BLOCK_TYPE_1G;
		goto hint_set;
	}

	if (new_size >= 0x200000) {     /* 2M */
		block_type = PMFS_BLOCK_TYPE_2M;
		goto hint_set;
	}

	/* defaulting to 4K */
	block_type = PMFS_BLOCK_TYPE_4K;

hint_set:
	pmfs_dbg_verbose(
		"Hint: new_size 0x%llx, i_size 0x%llx, root 0x%llx\n",
		new_size, pi->i_size, le64_to_cpu(pi->root));
	pmfs_dbg_verbose("Setting the hint to 0x%x\n", block_type);
	pmfs_memunlock_inode(sb, pi);
	pi->i_blk_type = block_type;
	pmfs_memlock_inode(sb, pi);
	return 0;
}

static long pmfs_fallocate(struct file *file, int mode, loff_t offset,
			    loff_t len)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	struct super_block *sb = inode->i_sb;
	long ret = 0;
	unsigned long blocknr, blockoff;
	int num_blocks, blocksize_mask;
	struct pmfs_inode *pi;
	pmfs_transaction_t *trans;
	loff_t new_size;
	printk(KERN_NOTICE "XIP_COW - pmfs_fallocate - entered \n");
	/* We only support the FALLOC_FL_KEEP_SIZE mode */
	if (mode & ~FALLOC_FL_KEEP_SIZE)
		return -EOPNOTSUPP;

	if (S_ISDIR(inode->i_mode))
		return -ENODEV;

	mutex_lock(&inode->i_mutex);

	new_size = len + offset;
	if (!(mode & FALLOC_FL_KEEP_SIZE) && new_size > inode->i_size) {
		ret = inode_newsize_ok(inode, new_size);
		if (ret)
			goto out;
	}

	pi = pmfs_get_inode(sb, inode->i_ino);
	if (!pi) {
		ret = -EACCES;
		goto out;
	}
		
	trans = pmfs_new_transaction(sb, MAX_INODE_LENTRIES +
			MAX_METABLOCK_LENTRIES);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out;
	}
	pmfs_add_logentry(sb, trans, pi, MAX_DATA_PER_LENTRY, LE_DATA);

	/* Set the block size hint */
	//pmfs_set_blocksize_hint(sb, pi, new_size);

	blocksize_mask = sb->s_blocksize - 1;
	blocknr = offset >> sb->s_blocksize_bits;
	blockoff = offset & blocksize_mask;
	num_blocks = (blockoff + len + blocksize_mask) >> sb->s_blocksize_bits;
	ret = pmfs_alloc_blocks(trans, inode, blocknr, num_blocks, true);

	inode->i_mtime = inode->i_ctime = CURRENT_TIME_SEC;

	pmfs_memunlock_inode(sb, pi);
	if (ret || (mode & FALLOC_FL_KEEP_SIZE)) {
		pi->i_flags |= cpu_to_le32(PMFS_EOFBLOCKS_FL);
	}

	if (!(mode & FALLOC_FL_KEEP_SIZE) && new_size > inode->i_size) {
		inode->i_size = new_size;
		pi->i_size = cpu_to_le64(inode->i_size);
	}
	pi->i_mtime = cpu_to_le32(inode->i_mtime.tv_sec);
	pi->i_ctime = cpu_to_le32(inode->i_ctime.tv_sec);
	pmfs_memlock_inode(sb, pi);

	pmfs_commit_transaction(sb, trans);

out:
	mutex_unlock(&inode->i_mutex);
	return ret;
}

static loff_t pmfs_llseek(struct file *file, loff_t offset, int origin)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	int retval;

	if (origin != SEEK_DATA && origin != SEEK_HOLE)
		return generic_file_llseek(file, offset, origin);

	mutex_lock(&inode->i_mutex);
	switch (origin) {
	case SEEK_DATA:
		retval = pmfs_find_region(inode, &offset, 0);
		if (retval) {
			mutex_unlock(&inode->i_mutex);
			return retval;
		}
		break;
	case SEEK_HOLE:
		retval = pmfs_find_region(inode, &offset, 1);
		if (retval) {
			mutex_unlock(&inode->i_mutex);
			return retval;
		}
		break;
	}

	if ((offset < 0 && !(file->f_mode & FMODE_UNSIGNED_OFFSET)) ||
	    offset > inode->i_sb->s_maxbytes) {
		mutex_unlock(&inode->i_mutex);
		return -EINVAL;
	}

	if (offset != file->f_pos) {
		file->f_pos = offset;
		file->f_version = 0;
	}

	mutex_unlock(&inode->i_mutex);
	return offset;
}

inline int pmfs_sync_abort(struct super_block *sb, pmfs_transaction_t *trans){
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	int ret = 0;
	mutex_lock(&sbi->s_lock);
	ret = pmfs_abort_transaction(sb, trans);
	mutex_unlock(&sbi->s_lock);

	return ret;	
}


static int pmfs_writeback(struct page *page, struct address_space *mapping, int emulate){
	
	struct inode    *inode = mapping->host;
	struct super_block *sb = inode->i_sb;
	void *xip_mem;
	int error;
	size_t remain = 0;
	pgoff_t pgoff;
	unsigned long xip_pfn;

	pgoff = page->index;
	//printk(KERN_NOTICE "XIP_COW - pmfs_writeback - page index %lx \n", page->index);
	//set_page_writeback(page);
	//printk(KERN_NOTICE "XIP_COW - pmfs_writeback entered\n");
	error = mapping->a_ops->get_xip_mem(mapping, pgoff, 0,&xip_mem, &xip_pfn);
	if (unlikely(error)) {
		printk(KERN_NOTICE "XIP_COW - pmfs_writeback - error\n");
		return -EINVAL;
	} else {
		/* flush the range */
		pmfs_xip_mem_protect(sb, xip_mem, PAGE_CACHE_SHIFT, 1);
		remain = __copy_from_user_inatomic_nocache(xip_mem, page_address(page), PAGE_CACHE_SIZE);
		pmfs_xip_mem_protect(sb, xip_mem, PAGE_CACHE_SHIFT, 0);
		if(remain)
			printk(KERN_NOTICE "XIP_COW - pmfs_writeback - remain %ld\n",remain);
		if(emulate)
			emulate_latency(PAGE_CACHE_SIZE - remain);
		
	}

	return 0;
}

static int pmfs_cow_sync(struct file *filp, struct inode *inode, loff_t start, loff_t end, int datasync){

	struct mm_struct *mm = current->mm;
	struct super_block *sb = inode->i_sb;
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	spinlock_t *ptl;
	struct page *page;
	struct pmfs_inode *pi;
	pte_t *ptep, ptec,**pte_list;
	__le64 *block;
	pmfs_transaction_t *trans;
	unsigned long addr, pfn, max_logentries,num_blocks;
	int i,ret = 0;
	int list_size = 0;
	end += 1;
	//printk(KERN_NOTICE "XIP_COW - pmfs_cow_sync - entered: start %llx end %llx \n",start, end);
	
	/*first update and log the inode*/
	sb_start_write(inode->i_sb);
	mutex_lock(&inode->i_mutex);

	pi = pmfs_get_inode(sb, inode->i_ino);

	num_blocks = ((end - start) >> sb->s_blocksize_bits) + 1;
	max_logentries = num_blocks + 2;
	//if (max_logentries > MAX_METABLOCK_LENTRIES)
	//	max_logentries = MAX_METABLOCK_LENTRIES;

	trans = pmfs_new_cow_transaction(sb,MAX_INODE_LENTRIES + max_logentries, pi->i_blk_type);
	if(datasync == 16)
		trans->free_blocks->i_opt = 1;

	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out;
	}

	pmfs_add_logentry(sb, trans, pi, MAX_DATA_PER_LENTRY, LE_DATA);

	ret = file_remove_suid(filp);
	if (ret) {
		pmfs_abort_transaction(sb, trans);
		goto out;
	}
	inode->i_ctime = inode->i_mtime = CURRENT_TIME_SEC;
	pmfs_update_time(inode, pi);
	addr = start;
	pte_list = vmalloc(num_blocks * sizeof(pte_t*));
	do{
		//printk(KERN_NOTICE "XIP_COW - pmfs_cow_sync - following address %lx \n",addr);
		ret = pte_follow(mm, addr, &ptep, &ptl);

		if(ret){
			//printk(KERN_NOTICE "XIP_COW - pmfs_cow_sync - error finding pte %d \n",ret);
			goto go_on;
		}
		pfn = pte_pfn(*ptep);
		if(!pfn_valid(pfn)){
			//printk(KERN_NOTICE "XIP_COW - pmfs_cow_sync - page is empty \n");
			pte_unmap_unlock(ptep,ptl);
			goto go_on;
		}
	
		page = pte_page(*ptep);
		if(!page){
			//printk(KERN_NOTICE "XIP_COW - pmfs_cow_sync - no page\n");
			goto go_on_unlock;
		}
		

		if(!pte_dirty(*ptep)){
			//printk(KERN_NOTICE "XIP_COW - pmfs_cow_sync - page not dirty\n");
			goto go_on_unlock;
		}
		pte_list[list_size] = ptep;
		list_size++;

go_on_unlock:	
		pte_unmap_unlock(ptep,ptl);
go_on:
		addr += PAGE_CACHE_SIZE;
	}while(addr < end);

	if(datasync == 14)
		goto writeback;
	
	mutex_lock(&sbi->s_lock);
	for(i=0;i<list_size;i++){
		ptep = pte_list[i];

		page = pte_page(*ptep);
		ret = pmfs_cow_block(trans,sb, pi, page->index);

		if(ret){
			printk("XIP_COW - pmfs_cow_sync cowing blocks error!!!\n");
			pmfs_abort_transaction(sb, trans);
			goto out;
		}
		
	}
	mutex_unlock(&sbi->s_lock);
writeback:
	if(datasync != 15)
		for(i=0;i<list_size;i++){
			ptep = pte_list[i];
			page = pte_page(*pte_list[i]);
		
			ret = pmfs_writeback(page, inode->i_mapping,datasync - 12);
		
			if(!ret){
				ptec = pte_mkclean(*ptep);
				set_pte(ptep, ptec);
				if(datasync == 17)
					pmfs_flush_buffer(page_address(page), PAGE_CACHE_SIZE, 0);

				/*if(PageDirty(page)){	
					lock_page(page);
					clear_page_dirty_for_io(page);
					unlock_page(page);
				}*/
			}
			else {
				printk("XIP_COW - pmfs_cow_sync writing back blocks error!!!\n");
				pmfs_sync_abort(sb, trans);
			}

			//pte_unmap_unlock(ptep,ptl);
			if(ret)
				goto out;
		}

	pmfs_commit_transaction(sb, trans);
out:
	if(pte_list)
		vfree(pte_list);
	mutex_unlock(&inode->i_mutex);
	sb_end_write(inode->i_sb);

	return ret;

}

/* This function is called by both msync() and fsync().
 * TODO: Check if we can avoid calling pmfs_flush_buffer() for fsync. We use
 * movnti to write data to files, so we may want to avoid doing unnecessary
 * pmfs_flush_buffer() on fsync() */
static int pmfs_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	if(!file){
		printk("NO FILE!   \n");
		return 0;
	}	
	
	/* Sync from start to end[inclusive] */
	struct address_space *mapping = file->f_mapping;
	if(!mapping){
		printk("NO MAPPING!   \n");
		return 0;
	}
	struct inode *inode = mapping->host;
	loff_t isize;
	int error;

	/* if the file is not mmap'ed, there is no need to do clflushes */
	if (mapping_mapped(mapping) == 0)
		goto persist;

	if(datasync >= 12)
		return	pmfs_cow_sync(file,inode,start,end,datasync);
	else if(datasync >=11)
		commit_atm_mapping(inode);
	
	end += 1; /* end is inclusive. We like our indices normal please ! */

	isize = i_size_read(inode);

	if ((unsigned long)end > (unsigned long)isize)
		end = isize;
	if (!isize || (start >= end))
	{
		pmfs_dbg_verbose("[%s:%d] : (ERR) isize(%llx), start(%llx),"
			" end(%llx)\n", __func__, __LINE__, isize, start, end);
		return -ENODATA;
	}


	/* Align start and end to cacheline boundaries */
	start = start & CACHELINE_MASK;
	end = CACHELINE_ALIGN(end);
	do {
		void *xip_mem;
		pgoff_t pgoff;
		loff_t offset;
		unsigned long xip_pfn, nr_flush_bytes;

		pgoff = start >> PAGE_CACHE_SHIFT;
		offset = start & ~PAGE_CACHE_MASK;

		nr_flush_bytes = PAGE_CACHE_SIZE - offset;
		if (nr_flush_bytes > (end - start))
			nr_flush_bytes = end - start;

		error = mapping->a_ops->get_xip_mem(mapping, pgoff, 0,
		&xip_mem, &xip_pfn);

		if (unlikely(error)) {
			/* sparse files could have such holes */
			pmfs_dbg_verbose("[%s:%d] : start(%llx), end(%llx),"
			" pgoff(%lx)\n", __func__, __LINE__, start, end, pgoff);
		} else {
			/* flush the range */
			pmfs_flush_buffer(xip_mem+offset, nr_flush_bytes, 0);
		}

		start += nr_flush_bytes;
	} while (start < end);
persist:
	PERSISTENT_MARK();
	PERSISTENT_BARRIER();
	return 0;
}

/* This callback is called when a file is closed */
static int pmfs_flush(struct file *file, fl_owner_t id)
{
	int ret = 0;
	/* if the file was opened for writing, make it persistent.
	 * TODO: Should we be more smart to check if the file was modified? */
	if (file->f_mode & FMODE_WRITE) {
		PERSISTENT_MARK();
		PERSISTENT_BARRIER();
	}

	return ret;
}

static unsigned long
pmfs_get_unmapped_area(struct file *file, unsigned long addr,
			unsigned long len, unsigned long pgoff,
			unsigned long flags)
{
	unsigned long align_size;
	struct vm_area_struct *vma;
	struct mm_struct *mm = current->mm;
	struct inode *inode = file->f_mapping->host;
	struct pmfs_inode *pi = pmfs_get_inode(inode->i_sb, inode->i_ino);
	struct vm_unmapped_area_info info;

	if (len > TASK_SIZE)
		return -ENOMEM;

	if (pi->i_blk_type == PMFS_BLOCK_TYPE_1G)
		align_size = PUD_SIZE;
	else if (pi->i_blk_type == PMFS_BLOCK_TYPE_2M)
		align_size = PMD_SIZE;
	else
		align_size = PAGE_SIZE;

	if (flags & MAP_FIXED) {
		/* FIXME: We could use 4K mappings as fallback. */
		if (len & (align_size - 1))
			return -EINVAL;
		if (addr & (align_size - 1))
			return -EINVAL;
		return addr;
	}

	if (addr) {
		addr = ALIGN(addr, align_size);
		vma = find_vma(mm, addr);
		if (TASK_SIZE - len >= addr &&
		    (!vma || addr + len <= vma->vm_start))
			return addr;
	}

	/*
	 * FIXME: Using the following values for low_limit and high_limit
	 * implicitly disables ASLR. Awaiting a better way to have this fixed.
	 */
	info.flags = 0;
	info.length = len;
	info.low_limit = TASK_UNMAPPED_BASE;
	info.high_limit = TASK_SIZE;
	info.align_mask = align_size - 1;
	info.align_offset = 0;
	return vm_unmapped_area(&info);
}

const struct file_operations pmfs_xip_file_operations = {
	.llseek			= pmfs_llseek,
	.read			= pmfs_xip_file_read,
	.write			= pmfs_xip_file_write,
	.aio_read		= xip_file_aio_read,
	.aio_write		= xip_file_aio_write,
	.mmap			= pmfs_xip_file_mmap,
	.open			= generic_file_open,
	.fsync			= pmfs_fsync,
	.flush			= pmfs_flush,
	.get_unmapped_area	= pmfs_get_unmapped_area,
	.unlocked_ioctl		= pmfs_ioctl,
	.fallocate		= pmfs_fallocate,
#ifdef CONFIG_COMPAT
	.compat_ioctl		= pmfs_compat_ioctl,
#endif
};

const struct inode_operations pmfs_file_inode_operations = {
	.setattr	= pmfs_notify_change,
	.getattr	= pmfs_getattr,
	.get_acl	= NULL,
};
