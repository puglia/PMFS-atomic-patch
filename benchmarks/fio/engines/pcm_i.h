#include <stdint.h>
#include <mmintrin.h>

#define _PCM_INTERNAL_H

#define CACHELINE_SIZE  (64)

#define NS2CYCLE(__ns) ((__ns) * M_PCM_CPUFREQ / 1000)
#define CYCLE2NS(__cycles) ((__cycles) * 1000 / M_PCM_CPUFREQ)

/* Hardware Cache */

#define BLOCK_ADDR(addr) ( (pcm_word_t *) (((pcm_word_t) (addr)) & ~(CACHELINE_SIZE - 1)) )
#define INDEX_ADDR(addr) ( (pcm_word_t *) (((pcm_word_t) (addr)) & (CACHELINE_SIZE - 1)) )

#define M_PCM_LATENCY_WRITE 30//150

#define M_PCM_BANDWIDTH_MB 1200

#define DRAM_BANDWIDTH_MB 7000

#define M_PCM_CPUFREQ 3000

typedef uintptr_t pcm_word_t;
typedef uint64_t pcm_hrtime_t;

//spinlock_t pcm_lock;

extern void emulate_latency(size_t size);

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


 void emulate_latency(size_t size){
	int              extra_latency;
	extra_latency = (int) size * (1-(float) (((float) M_PCM_BANDWIDTH_MB)/1000)/(((float) DRAM_BANDWIDTH_MB)/1000))/(((float)M_PCM_BANDWIDTH_MB)/1000);
	//spin_lock(&pcm_lock);
	emulate_latency_ns(extra_latency);
	//spin_unlock(&pcm_lock);
}

static inline void pmfs_flush_buffer(void *buf, uint32_t len, int fence)
{
	uint32_t i;
	len = len + ((unsigned long)(buf) & (CACHELINE_SIZE - 1));
	for (i = 0; i < len; i += CACHELINE_SIZE)
		asm volatile ("clflush %0\n" : "+m" (*(char *)(buf+i)));
	/* Do a fence only if asked. We often don't need to do a fence
	 * immediately after clflush because even if we get context switched
	 * between clflush and subsequent fence, the context switch operation
	 * provides implicit fence. */
	if (fence)
		asm volatile ("sfence\n" : : );
}

