
class timing_c
{
private:
	int64_t	start;
	int64_t	end;

	int		reset;
public:
	timing_c(void)
	{
	}
	void Start()
	{
#ifdef _MSVC_VER
		const int64_t *s = &start;
		__asm
		{
			push eax
			push ebx
			push edx

			rdtsc
			mov ebx, s
			mov	[ebx], eax
			mov [ebx + 4], edx

			pop edx
			pop ebx
			pop eax
		}
#else
		asm("rdtsc" : "=A"(start));
#endif
	}
	int End()
	{
		int64_t	time;
#ifdef _MSVC_VER
		const int64_t *e = &end;
		__asm
		{
			push eax
			push ebx
			push edx

			rdtsc
			mov ebx, e
			mov	[ebx], eax
			mov [ebx + 4], edx

			pop edx
			pop ebx
			pop eax
		}
#else
		asm("rdtsc" : "=A"(time));
#endif 
		time = end - start;
		if (time < 0)
		{
			time = 0;
		}
		return((int)time);
	}
};
// end
