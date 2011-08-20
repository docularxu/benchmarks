#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <zlib.h>
#include <time.h>
#include <sys/resource.h>
#include "kseq.h"
KSEQ_INIT(gzFile, gzread)

#define kroundup32(x) (--(x), (x)|=(x)>>1, (x)|=(x)>>2, (x)|=(x)>>4, (x)|=(x)>>8, (x)|=(x)>>16, ++(x))

int ksa_sa(const unsigned char *T, int *SA, int n, int k);
int sais_int(const int *T, int *SA, int n, int k);
int sais(const unsigned char *T, int *SA, int n);
void suffixsort(int *x, int *p, int n, int k, int l);

unsigned char seq_nt6_table[128];
void seq_char2nt6(int l, unsigned char *s);
void seq_revcomp6(int l, unsigned char *s);
uint32_t SA_checksum(int l, const int *s);
double rssmem();

int main(int argc, char *argv[])
{
	kseq_t *seq;
	gzFile fp;
	int *SA, c, l = 0, max = 0, algo = 0, n_sentinels = 0, has_sentinel = 1;
	uint8_t *s = 0;
	clock_t t = clock();

	while ((c = getopt(argc, argv, "a:x")) >= 0) {
		switch (c) {
			case 'a':
				if (strcmp(argv[2], "ksa") == 0) algo = 0;
				else if (strcmp(argv[2], "qsufsort") == 0) algo = 1;
				else if (strcmp(argv[2], "sais") == 0) algo = 2;
				else {
					fprintf(stderr, "(EE) Unknown algorithm.\n");
					return 1;
				}
				break;
			case 'x': has_sentinel = 0; break;
		}
	}
	if (argc == optind) {
		fprintf(stderr, "\n");
		fprintf(stderr, "Usage:   mssac [options] input.fasta\n\n");
		fprintf(stderr, "Options: -a STR    algorithm: ksa, sais, qsufsort [ksa]\n");
		fprintf(stderr, "         -x        do not regard a NULL as a sentinel\n");
		fprintf(stderr, "\n");
		return 1;
	}

	fp = gzopen(argv[optind], "r");
	seq = kseq_init(fp);
	while (kseq_read(seq) >= 0) {
		if (l + (seq->seq.l + 1) * 2 + 1 >= max) {
			max = l + (seq->seq.l + 1) * 2 + 2;
			kroundup32(max);
			s = realloc(s, max);
		}
		seq_char2nt6(seq->seq.l, (uint8_t*)seq->seq.s);
		memcpy(s + l, seq->seq.s, seq->seq.l + 1);
		l += seq->seq.l + 1;
		seq_revcomp6(seq->seq.l, (uint8_t*)seq->seq.s);
		memcpy(s + l, seq->seq.s, seq->seq.l + 1);
		l += seq->seq.l + 1;
		n_sentinels += 2;
	}
	s[l] = 0;
	kseq_destroy(seq);
	gzclose(fp);
	fprintf(stderr, "(MM) Read %d symbols in %.3f seconds.\n", l, (double)(clock() - t) / CLOCKS_PER_SEC);

	t = clock();
	if (has_sentinel) { // A NULL is regarded a sentinel
		if (algo == 0) { // ksa
			SA = (int*)malloc(sizeof(int) * l);
			ksa_sa(s, SA, l, 6);
			SA_checksum(l, SA);
			free(SA); free(s);
		} else if (has_sentinel && (algo == 1 || algo == 2)) { // sais or qsufsort
			int i, *tmp, k = 0;
			tmp = (int*)malloc(sizeof(int) * (l + 1));
			for (i = 0; i < l; ++i)
				tmp[i] = s[i]? n_sentinels + s[i] : ++k;
			free(s);
			SA = (int*)malloc(sizeof(int) * (l + 1));
			if (algo == 1) { // qsufsort
				suffixsort(tmp, SA, l, n_sentinels + 6, 1);
				SA_checksum(l, SA + 1);
			} else if (algo == 2) { // sais
				sais_int(tmp, SA, l, n_sentinels + 6);
				SA_checksum(l, SA);
			}
			free(SA); free(tmp);
		}
	} else { // A NULL is regarded as an ordinary symbol
		if (algo == 0) { // ksa
			int i;
			for (i = 0; i < l; ++i) ++s[i];
			SA = (int*)malloc(sizeof(int) * l);
			ksa_sa(s, SA, l + 1, 7);
			SA_checksum(l, SA + 1);
			free(SA); free(s);
		} else if (algo == 1) { // qsufsort
			int i, *tmp;
			tmp = (int*)malloc(sizeof(int) * (l + 1));
			for (i = 0; i < l; ++i) tmp[i] = s[i];
			free(s);
			SA = (int*)malloc(sizeof(int) * (l + 1));
			suffixsort(tmp, SA, l, 6, 0);
			SA_checksum(l, SA + 1);
			free(SA); free(tmp);
		} else if (algo == 2) { // sais
			SA = (int*)malloc(sizeof(int) * l);
			sais(s, SA, l);
			SA_checksum(l, SA);
			free(SA); free(s);
		}
	}
	fprintf(stderr, "(MM) Constructed suffix array in %.3f seconds.\n", (double)(clock() - t) / CLOCKS_PER_SEC);

#ifndef __linux__
	fprintf(stderr, "(MM) Max RSS: %.3f MB\n", rssmem());
#endif
	return 0;
}

unsigned char seq_nt6_table[128] = {
    0, 5, 5, 5,  5, 5, 5, 5,  5, 5, 5, 5,  5, 5, 5, 5,
    5, 5, 5, 5,  5, 5, 5, 5,  5, 5, 5, 5,  5, 5, 5, 5,
    5, 5, 5, 5,  5, 5, 5, 5,  5, 5, 5, 5,  5, 5, 5, 5,
    5, 5, 5, 5,  5, 5, 5, 5,  5, 5, 5, 5,  5, 5, 5, 5,
    5, 1, 5, 2,  5, 5, 5, 3,  5, 5, 5, 5,  5, 5, 5, 5,
    5, 5, 5, 5,  4, 5, 5, 5,  5, 5, 5, 5,  5, 5, 5, 5,
    5, 1, 5, 2,  5, 5, 5, 3,  5, 5, 5, 5,  5, 5, 5, 5,
    5, 5, 5, 5,  4, 5, 5, 5,  5, 5, 5, 5,  5, 5, 5, 5
};

void seq_char2nt6(int l, unsigned char *s)
{
	int i;
	for (i = 0; i < l; ++i)
		s[i] = s[i] < 128? seq_nt6_table[s[i]] : 5;
}

void seq_revcomp6(int l, unsigned char *s)
{
	int i;
	for (i = 0; i < l>>1; ++i) {
		int tmp = s[l-1-i];
		tmp = (tmp >= 1 && tmp <= 4)? 5 - tmp : tmp;
		s[l-1-i] = (s[i] >= 1 && s[i] <= 4)? 5 - s[i] : s[i];
		s[i] = tmp;
	}
	if (l&1) s[i] = (s[i] >= 1 && s[i] <= 4)? 5 - s[i] : s[i];
}

uint32_t SA_checksum(int l, const int *s)
{
	uint32_t h = *s;
	const int *end = s + l;
	clock_t t = clock();
	for (; s < end; ++s) h = (h << 5) - h + *s;
	fprintf(stderr, "(MM) Computed SA X31 checksum in %.3f seconds (checksum = %x)\n", (double)(clock() - t) / CLOCKS_PER_SEC, h);
	return h;
}

double rssmem() // not working in Linux
{
    struct rusage r;
    getrusage(RUSAGE_SELF, &r);
    return r.ru_maxrss / 1024.0 / 1024.0;
}
