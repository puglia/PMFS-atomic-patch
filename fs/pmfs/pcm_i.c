
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include "pcm_i.h"
#include "pmfs.h"

spinlock_t pcm_lock;
int error_occurred = 0;
struct pmfs_sb_info *superbloco;

 void emulate_latency(size_t size){
	int              extra_latency;
	extra_latency = (int) size * (1-(float) (((float) M_PCM_BANDWIDTH_MB)/1000)/(((float) DRAM_BANDWIDTH_MB)/1000))/(((float)M_PCM_BANDWIDTH_MB)/1000);
	spin_lock(&pcm_lock);
	emulate_latency_ns(extra_latency);
	spin_unlock(&pcm_lock);
}

void set_sb(struct super_block *sb){
	superbloco = PMFS_SB(sb);
}

void lock_first(){
	mutex_lock(&superbloco->s_lock);
	mutex_unlock(&superbloco->s_lock);
}

void set_error(){
	error_occurred++;
}

int get_error(){
	return error_occurred;
}

void reset_error(){
	error_occurred = 0;
}
