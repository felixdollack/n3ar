#include <stdio.h>
#include <stdlib.h>
//#include <malloc.h> // is part of stdlib
#include <memory.h>
#include <math.h>

#include <stdint.h>

/*
	
*/

#define NRANDS 256

/*
	portable time function
*/

#ifdef __GNUC__
#include <time.h>
float getticks()
{
	struct timespec ts;

	if(clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
	{
		printf("clock_gettime error\n");

		return -1.0f;
	}

	return ts.tv_sec + 1e-9f*ts.tv_nsec;
}
#else
#include <windows.h>
float getticks()
{
	static double freq = -1.0;
	LARGE_INTEGER lint;

	if(freq < 0.0)
	{
		if(!QueryPerformanceFrequency(&lint))
			return -1.0f;

		freq = lint.QuadPart;
	}

	if(!QueryPerformanceCounter(&lint))
		return -1.0f;

	return (float)( lint.QuadPart/freq );
}
#endif

/*
	multiply with carry PRNG
*/

uint32_t mwcrand_r(uint64_t* state)
{
	uint32_t* m;

	//
	m = (uint32_t*)state;

	// bad state?
	if(m[0] == 0)
		m[0] = time(0);

	if(m[1] == 0)
		m[1] = time(0);

	// mutate state
	m[0] = 36969 * (m[0] & 65535) + (m[0] >> 16);
	m[1] = 18000 * (m[1] & 65535) + (m[1] >> 16);

	// output
	return (m[0] << 16) + m[1];
}

uint64_t prngglobal = 0x12345678000fffffLL;

void smwcrand(uint32_t seed)
{
	prngglobal = 0x12345678000fffffLL*seed;
}

uint32_t mwcrand()
{
	return mwcrand_r(&prngglobal);
}

/*
	
*/

#define BINTEST(r, c, t, pixels, ldim) ( (pixels)[(r)*(ldim)+(c)] > (t) )

float compute_entropy(int atoms[], int inds[], int ninds)
{
	int i;
	int counts[256];
	double h;
	
	//
	memset(counts, 0, 256*sizeof(int));
	
	//
	for(i=0; i<ninds; ++i)
		++counts[ atoms[inds[i]] ];
	
	//
	h = 0.0;
	
	for(i=0; i<256; ++i)
	{
		double p;

		if(counts[i])
		{
			p = counts[i]/(double)ninds;
			
			h += -p*log2(p);
		}
	}
	
	//
	return (float)h;
}

float compute_split_entropy(int r, int c, int t, uint8_t* ppixels[], int ldim, int atoms[], int inds[], int ninds)
{
	int i;
	int n0, n1;
	int counts0[256], counts1[256];
	
	double h0, h1;

	//
	n0 = 0;
	n1 = 0;
	
	memset(counts0, 0, 256*sizeof(int));
	memset(counts1, 0, 256*sizeof(int));
	
	//
	for(i=0; i<ninds; ++i)
		if( 1==BINTEST(r, c, t, ppixels[inds[i]], ldim) )
		{
			++n1;
			++counts1[ atoms[inds[i]] ];
		}
		else
		{
			++n0;
			++counts0[ atoms[inds[i]] ];
		}

	//
	h0 = 0.0;
	h1 = 0.0;

	for(i=0; i<256; ++i)
	{
		double p;

		if(counts0[i])
		{
			p = counts0[i]/(double)n0;
			
			h0 += -p*log2(p);
		}
		
		if(counts1[i])
		{
			p = counts1[i]/(double)n1;
			
			h1 += -p*log2(p);
		}
	}

	//
	return (float)( (n0*h0+n1*h1)/(n0+n1) );
}

int split(int r, int c, int t, uint8_t* ppixels[], int ldim, int inds[], int ninds)
{
	int stop;
	int i, j;

	int n0;

	//
	stop = 0;

	i = 0;
	j = ninds - 1;

	while(!stop)
	{
		//
		while( 0==BINTEST(r, c, t, ppixels[inds[i]], ldim) )
		{
			if( i==j )
				break;
			else
				++i;
		}

		while( 1==BINTEST(r, c, t, ppixels[inds[j]], ldim) )
		{
			if( i==j )
				break;
			else
				--j;
		}

		//
		if( i==j )
			stop = 1;
		else
		{
			// swap
			inds[i] = inds[i] ^ inds[j];
			inds[j] = inds[i] ^ inds[j];
			inds[i] = inds[i] ^ inds[j];
		}
	}

	//
	n0 = 0;

	for(i=0; i<ninds; ++i)
		if( 0==BINTEST(r, c, t, ppixels[inds[i]], ldim) )
			++n0;

	//
	return n0;
}

int learn_subtree(int32_t* nodes, int nodeidx, int depth, int maxdepth, int atoms[], uint8_t* ppixels[], int nrows, int ncols, int inds[], int ninds)
{
	int i;
	uint8_t* n;

	int nrands;
	int rs[NRANDS], cs[NRANDS], ts[NRANDS];

	float hs[NRANDS], hmin;
	int best;

	//
	n = (uint8_t*)&nodes[nodeidx];

	//
	if(ninds == 0)
	{
		// construct a terminal node (leaf)
		n[0] = 0;
		n[1] = 0;
		n[2] = 0;
		n[3] = 0; // irrelevant

		//
		return 1;
	}
	else if(depth == maxdepth)
	{
		int max, atom;

		#define MAXNATOMS (2048)
		int counts[MAXNATOMS];

		//
		memset(counts, 0, MAXNATOMS*sizeof(int));

		for(i=0; i<ninds; ++i)
			++counts[ atoms[inds[i]] ];

		//
		max = counts[0];
		atom = 0;

		for(i=1; i<MAXNATOMS; ++i)
			if(counts[i] > max)
			{
				max = counts[i];
				atom = i;
			}

		// construct terminal node (leaf)
		n[0] = 0;
		n[1] = atom%256;
		n[2] = atom/256;
		n[3] = 0; // irrelevant

		//
		return 1;
	}
	else if(compute_entropy(atoms, inds, ninds) == 0.0f)
	{
		// construct terminal node (leaf)
		n[0] = 0;
		n[1] = atoms[inds[0]]%256;
		n[2] = atoms[inds[0]]/256;
		n[3] = 0; // irrelevant

		//
		return 1;
	}

	//
	nrands = NRANDS;

	for(i=0; i<nrands; ++i)
	{
		rs[i] = mwcrand()%nrows;
		cs[i] = mwcrand()%ncols;
		ts[i] = mwcrand()%256;
	}

	//
	#pragma omp parallel for
	for(i=0; i<nrands; ++i)
		hs[i] = compute_split_entropy(rs[i], cs[i], ts[i], ppixels, ncols, atoms, inds, ninds);

	//
	hmin = hs[0];
	best = 0;

	for(i=1; i<nrands; ++i)
		if(hs[i] < hmin)
		{
			hmin = hs[i];
			best = i;
		}

	// construct a nonterminal node
	n[0] = 1;
	n[1] = rs[best];
	n[2] = cs[best];
	n[3] = ts[best];

	// recursively buils two subtrees
	i = split(rs[best], cs[best], ts[best], ppixels, ncols, inds, ninds);

	learn_subtree(nodes, 2*nodeidx+1, depth+1, maxdepth, atoms, ppixels, nrows, ncols, &inds[0], i);
	learn_subtree(nodes, 2*nodeidx+2, depth+1, maxdepth, atoms, ppixels, nrows, ncols, &inds[i], ninds-i);

	//
	return 1;
}

int* learn_tree(int atoms[], uint8_t* ppixels[], int nsamples, int nrows, int ncols, int tdepth)
{
	int i, nnodes;

	int32_t* tree = 0;
	int* inds = 0;

	//
	nnodes = (1<<(tdepth+1)) - 1;

	//
	tree = (int*)malloc((nnodes+1)*sizeof(int32_t));

	if(!tree)
		return 0;

	// initialize
	tree[0] = tdepth;

	memset(&tree[1], 0, nnodes*sizeof(int32_t)); // all nodes are terminal, for now

	//
	inds = (int*)malloc( nsamples*sizeof(int) );
	
	if(!inds)
	{
		//
		free(tree);

		//
		return 0;
	}
	
	for(i=0; i<nsamples; ++i)
		inds[i] = i;
	
	//
	if(!learn_subtree(&tree[1], 0, 0, tdepth, atoms, ppixels, nrows, ncols, inds, nsamples))
	{
		//
		free(tree);
		free(inds);

		//
		return 0;
	}

	//
	free(inds);

	//
	return tree;
}

/*
	
*/

#define MAXNSAMPLES 40000000

static int nsamples=0;

static int nrows, ncols;

static uint8_t* ppixels[MAXNSAMPLES];
static int atoms[MAXNSAMPLES];

int load_data(const char* path)
{
	int i;

	FILE* src;

	//
	src = fopen(path, "rb");

	if(!src)
		return 0;

	//
	fread(&nsamples, 1, sizeof(int), src);

	if(nsamples > MAXNSAMPLES)
		nsamples = MAXNSAMPLES;

	//
	fread(&nrows, 1, sizeof(int), src);
	fread(&ncols, 1, sizeof(int), src);

	//
	for(i=0; i<nsamples; ++i)
	{
		//
		fread(&atoms[i], 1, sizeof(int), src);

		//
		ppixels[i] = (uint8_t*)malloc(nrows*ncols*sizeof(uint8_t));
		fread(ppixels[i], nrows*ncols, sizeof(uint8_t), src);
	}

	//
	fclose(src);

	//
	return 1;
}

/*
	
*/

int main(int argc, char* argv[])
{
	int i;
	float t;

	int tdepth;

	int* tree = 0;

	//
	if(argc != 4)
	{
		printf("* specify arguments: training data, tree depth, output file name\n");
		return 0;
	}

	// initialize PRNG
	smwcrand(time(0));

	//
	if(!load_data(argv[1]))
	{
		printf("* cannot load training data ... exiting ...\n");
		return 1;
	}

	printf("* %d training samples ...\n", nsamples);

	//
	sscanf(argv[2], "%d", &tdepth);

	//
	t = getticks();

	tree = learn_tree(atoms, ppixels, nsamples, nrows, ncols, tdepth);

	printf("* elapsed time: %f [sec]\n", getticks()-t);
	printf("\n");

	//
	if(tree)
	{
		FILE* file = fopen(argv[3], "wb");
		fwrite(tree, sizeof(int32_t), (1<<(tdepth+1)), file);
		fclose(file);
	}
	else
		printf("* failed to learn tree ... exiting ...\n");

	//
	return 0;
}