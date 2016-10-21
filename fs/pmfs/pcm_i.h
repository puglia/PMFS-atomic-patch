#include <linux/random.h>
#include <linux/sched.h>
#include "pmfs.h"


#define _PCM_INTERNAL_H

#define NS2CYCLE(__ns) ((__ns) * M_PCM_CPUFREQ / 1000)
#define CYCLE2NS(__cycles) ((__cycles) * 1000 / M_PCM_CPUFREQ)

/* Hardware Cache */

#define BLOCK_ADDR(addr) ( (pcm_word_t *) (((pcm_word_t) (addr)) & ~(CACHELINE_SIZE - 1)) )
#define INDEX_ADDR(addr) ( (pcm_word_t *) (((pcm_word_t) (addr)) & (CACHELINE_SIZE - 1)) )

#define M_PCM_LATENCY_WRITE 150

#define M_PCM_BANDWIDTH_MB 1200

#define DRAM_BANDWIDTH_MB 7000

#define M_PCM_CPUFREQ 3000

#define TOTAL_OUTCOMES_NUM 10000000

#define CRASH_LIKELIHOOD 50



typedef uintptr_t pcm_word_t;
typedef uint64_t pcm_hrtime_t;

extern void emulate_latency(size_t size);
extern void set_error();
extern void reset_error();
extern int get_error();
extern void lock_first();
extern void set_sb(struct super_block *sb);
extern void set_inode(struct inode *inode);

static inline void asm_cpuid(void) {
	asm volatile( "cpuid" :::"rax", "rbx", "rcx", "rdx");
}

#if defined(__i386__)

static inline unsigned long long asm_rdtsc(void)
{
	unsigned long long int x;
	__asm__ volatile (".byte 0x0f, 0x31" : "=A" (x));
	return x;
}

static inline unsigned long long asm_rdtscp(void)
{
		unsigned hi, lo;
	__asm__ __volatile__ ("rdtscp" : "=a"(lo), "=d"(hi)::"ecx");
    return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );

}
#elif defined(__x86_64__)

static inline unsigned long long asm_rdtsc(void)
{
	unsigned hi, lo;
	__asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}

static inline unsigned long long asm_rdtscp(void)
{
	unsigned hi, lo;
	__asm__ __volatile__ ("rdtscp" : "=a"(lo), "=d"(hi)::"rcx");
    return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}
#else
#error "What architecture is this???"
#endif


static inline void asm_sse_write_block64(volatile pcm_word_t *addr, pcm_word_t *val)
{
	__asm__ __volatile__ ("movnti %1, %0" : "=m"(*&addr[0]): "r" (val[0]));
	__asm__ __volatile__ ("movnti %1, %0" : "=m"(*&addr[1]): "r" (val[1]));
	__asm__ __volatile__ ("movnti %1, %0" : "=m"(*&addr[2]): "r" (val[2]));
	__asm__ __volatile__ ("movnti %1, %0" : "=m"(*&addr[3]): "r" (val[3]));
	__asm__ __volatile__ ("movnti %1, %0" : "=m"(*&addr[4]): "r" (val[4]));
	__asm__ __volatile__ ("movnti %1, %0" : "=m"(*&addr[5]): "r" (val[5]));
	__asm__ __volatile__ ("movnti %1, %0" : "=m"(*&addr[6]): "r" (val[6]));
	__asm__ __volatile__ ("movnti %1, %0" : "=m"(*&addr[7]): "r" (val[7]));
}


static inline void asm_movnti(volatile pcm_word_t *addr, pcm_word_t val)
{
	__asm__ __volatile__ ("movnti %1, %0" : "=m"(*addr): "r" (val));
}


static inline void asm_clflush(volatile pcm_word_t *addr)
{
	__asm__ __volatile__ ("clflush %0" : : "m"(*addr));
}


static inline void asm_mfence(void)
{
	__asm__ __volatile__ ("mfence");
}


static inline void asm_sfence(void)
{
	__asm__ __volatile__ ("sfence");
}


static inline
int rand_int(unsigned int *seed)
{
    *seed=*seed*196314165+907633515;
    return *seed;
}


# ifdef _EMULATE_LATENCY_USING_NOPS
/* So you think nops are more accurate? you might be surprised */
static inline void asm_nop10() {
	asm volatile("nop");
	asm volatile("nop");
	asm volatile("nop");
	asm volatile("nop");
	asm volatile("nop");
	asm volatile("nop");
	asm volatile("nop");
	asm volatile("nop");
	asm volatile("nop");
	asm volatile("nop");
}

static inline
void
emulate_latency_ns(int ns)
{
	int          i;
	pcm_hrtime_t cycles;
	pcm_hrtime_t start;
	pcm_hrtime_t stop;
	printk("emulating latency: %dns",ns);
	cycles = NS2CYCLE(ns);
	for (i=0; i<cycles; i+=5) {
		asm_nop10(); /* each nop is 1 cycle */
	}
}

# else

static inline
void
emulate_latency_ns(int ns)
{
	pcm_hrtime_t cycles;
	pcm_hrtime_t start;
	pcm_hrtime_t stop;
	//printk("emulating latency: %dns",ns);
	start = asm_rdtsc();
	cycles = NS2CYCLE(ns);

	do { 
		/* RDTSC doesn't necessarily wait for previous instructions to complete 
		 * so a serializing instruction is usually used to ensure previous 
		 * instructions have completed. However, in our case this is a desirable
		 * property since we want to overlap the latency we emulate with the
		 * actual latency of the emulated instruction. 
		 */
		stop = asm_rdtsc();
	} while (stop - start < cycles);
}

# endif

static inline int should_crash(){
	unsigned int buf,random_number;
	int *a,*b;
	unsigned long long *seed;
	if(get_error())
		return 0;
	seed = asm_rdtsc();
	get_random_bytes(&buf,sizeof(buf));
	random_number = buf % TOTAL_OUTCOMES_NUM;
	//printk("attempt_crash: buf:%d   random_number:%d  \n",buf,random_number);
	if (random_number <= CRASH_LIKELIHOOD){
		printk("attempt_crash: buf:%d   random_number:%d  \n",buf,random_number);
		return 1;
	}

	return 0;
}

static inline int attempt_crash(char *message,int force){
	
	if (should_crash() || force) {
		set_error();
		lock_first();
		printk("Crashed!  - %s\n",message);
		BUG();//memcpy(a,b,64);
		return 1;
	}

	return 0;
}
