
#include <linux/types.h>
#include <linux/spinlock.h>
#include "pcm_i.h"

spinlock_t pcm_lock;

 void emulate_latency(size_t size){
	int              extra_latency;
	extra_latency = (int) size * (1-(float) (((float) M_PCM_BANDWIDTH_MB)/1000)/(((float) DRAM_BANDWIDTH_MB)/1000))/(((float)M_PCM_BANDWIDTH_MB)/1000);
	//spin_lock(&pcm_lock);
	emulate_latency_ns(extra_latency);
	//spin_unlock(&pcm_lock);
}
