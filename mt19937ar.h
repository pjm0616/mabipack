#pragma once

class mt19937ar
{
public:
	mt19937ar();
	mt19937ar(unsigned long s)
	: mt19937ar()
	{
		init_genrand(s);
	}
	~mt19937ar();

	/* initializes mt[N] with a seed */
	void init_genrand(unsigned long s);

	/* initialize by an array with array-length */
	/* init_key is the array for initializing keys */
	/* key_length is its length */
	/* slight change for C++, 2004/2/26 */
	void init_by_array(unsigned long init_key[], int key_length);

	/* generates a random number on [0,0xffffffff]-interval */
	unsigned long genrand_int32(void);

private:
	static const int N = 624;
	static const int M = 397;
	/* constant vector a */
	static const unsigned long MATRIX_A = 0x9908b0dfUL;
	/* most significant w-r bits */
	static const unsigned long UPPER_MASK = 0x80000000UL;
	/* least significant r bits */
	static const unsigned long LOWER_MASK = 0x7fffffffUL;

	unsigned long mt[N]; /* the array for the state vector  */
	int mti; /* mti==N+1 means mt[N] is not initialized */
};

