#ifndef __FLAGOPT_H__
#define __FLAGOPT_H__

static inline int test_flag(unsigned long *flags, unsigned long flag)
{
	unsigned long mask=1UL<<flag;

	if(flag<sizeof(unsigned long)<<3){
		return ((*flags & mask)!=0);
	}
	return 0;
}

static inline void set_flag(unsigned long *flags, unsigned long flag)
{
	unsigned long mask=1UL<<flag;
	
	if(flag<sizeof(unsigned long)<<3){
		*flags |= mask;
	}
}

static inline void clear_flag(unsigned long *flags, unsigned long flag)
{
	unsigned long mask=1UL<<flag;
	
	if(flag<sizeof(unsigned long)<<3){
		*flags &= ~mask;
	}
}

static inline int test_and_set_flag(unsigned long *flags, unsigned long flag)
{
	int ret=0;
	unsigned long mask=1UL<<flag;

	if(flag<sizeof(unsigned long)<<3){
		ret = ((*flags & mask)!=0);
		*flags |= mask;
	}
	return ret;
}


#endif	//__FLAGOPT_H__

