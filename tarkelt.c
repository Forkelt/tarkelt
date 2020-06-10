#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define TAR_MAGIC "ustar" /* With null. */
#define TAR_VERSION "00" /* No null. */
#define BLOCK_SIZE 512
#define UNEXPECTED_EOF -1

#define UNKNOWN_OPTION "%s: unknown option %s\n"
#define MISSING_OPTIONS "%s: need at least one option\n"


struct posix_header {
	char name[100];
	char mode[8];
	char uid[8];
	char gid[8];
	char size[12];
	char mtime[12];
	char chksum[8];
	char typeflag;
	char link_name[100];
	char magic[6];
	char version[2];
	char uname[32];
	char gname[32];
	char dev_major[8];
	char dev_minor[8];
	char prefix[155];
	char pad[12];
};


int check_zero_block(void *memory)
{
	size_t i = 0;
	while (i < BLOCK_SIZE) {
		if (((char*) memory)[i++]) {
			return 0;
		}
	}
	return 1;
}


int last_zero_block(FILE *fp, int blocks)
{
	char buff[512];
	if (fread(buff, 1, 512, fp) < 512 || !check_zero_block((void*) &buff)) {
		return UNEXPECTED_EOF;
	}
	if (feof(fp)) {
		return 0;
	}
	return blocks + 1;
}


int read_headers(FILE *fp)
{
	struct posix_header head;
	long file_size;
	int blocks = 0;

	for (;;) {
		if (feof(fp)) {
			return 0;
		}
		if (fread(&head, 1, 512, fp) < 512) {
			return UNEXPECTED_EOF;
		}
		if (check_zero_block((void*) &head)) {
			return last_zero_block(fp, blocks);
		}

		printf("%s\n", head.name);

		sscanf(head.size, "%lo", &file_size);
		file_size += 512 - file_size % 512;
		fseek(fp, file_size, SEEK_CUR);
		++blocks;
	}
}


int main(int argc, char *argv[])
{
	char *prog = argv[0];
	char *file;
	int truncatec = argc - 3;

	if (argc < 2) {
		fprintf(stderr, MISSING_OPTIONS, prog);
		return 2;
	}

	for (int i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "-f")) {
			file = argv[++i];
		} else if (!strcmp(argv[i], "-t")) {
			// TODO
		} else {
			fprintf(stderr, UNKNOWN_OPTION, prog, argv[i]);
			return 2;
		}
	}

	FILE *fp = fopen(file, "rb");
	read_headers(fp);
	fclose(fp);
	return 0;
}
