

#include <linux/module.h>
#include <linux/string.h>
#include <linux/types.h>
#include <asm/atomic.h>

#define REQUEST_QUEUE_SIZE 100

pmfs_free_block_request_t	*requests[REQUEST_QUEUE_SIZE];
atomic_t request_index = ATOMIC_INIT(0);

int enqueue_request(pmfs_free_block_request_t *request){
	requests[atomic_read(&request_index)] = request;
	if(atomic_read(&request_index) + 1 == REQUEST_QUEUE_SIZE)
		return 1;
	else
		atomic_inc(&request_index);

	return 0;
}

pmfs_free_block_request_t *dequeue_request(){
	if(atomic_read(&request_index) == 0)
		return NULL;

	pmfs_free_block_request_t *temp ;

	temp = requests[atomic_read(&request_index) - 1];
	atomic_dec(&request_index);
	//printk("return request \n");
	return temp;
}

int is_queue_empty(){
	if(atomic_read(&request_index) == 0)
		return 1;
	return 0;
}

pmfs_free_block_request_t *init_request(struct super_block *sb,
		pmfs_transaction_t *trans){
	pmfs_free_block_request_t *request;
	request = vmalloc(sizeof(pmfs_free_block_request_t));
	request->trans_t = trans;
	request->sb_t = sb;
	return request;
}

