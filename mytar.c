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


struct header {
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
	char pad[12]; /* For a clean 512 bytes, avoids certain issues. */
};


int zero_block(void *memory)
{
	size_t i = 0;

	while (i < BLOCK_SIZE)
		if (((char*) memory)[i++])
			return 0;

	return 1;
}


int last_block(FILE *fp)
{
	char buffer[BLOCK_SIZE];

	if (!fread(buffer, BLOCK_SIZE, 1, fp) || !zero_block((void*) &buffer))
		return UNEXPECTED_EOF;
	if (feof(fp))
		return 0;
	
	return 1;
}


void block_align(FILE *fp)
{
	fseek(fp, (BLOCK_SIZE - ftell(fp) % BLOCK_SIZE) % BLOCK_SIZE, SEEK_CUR);
}


int read_headers(FILE *fp)
{
	struct header head;
	long file_size;

	for (;;) {
		if (feof(fp))
			return 0;

		if (!fread(&head, sizeof(head), 1, fp))
			return UNEXPECTED_EOF;

		if (zero_block((void*) &head))
			return last_block(fp);

		printf("%s\n", head.name);

		sscanf(head.size, "%lo", &file_size);
		fseek(fp, file_size, SEEK_CUR);
		block_align(fp);
	}
}


int main(int argc, char *argv[])
{
	char *prog = argv[0];
	char *file;

	if (argc < 2) {
		fprintf(stderr, MISSING_OPTIONS, prog);
		return 2;
	}

	for (int i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "-f")) {
			file = argv[++i];
		} else if (!strcmp(argv[i], "-t")) {
			/* TODO */
		} else {
			fprintf(stderr, UNKNOWN_OPTION, prog, argv[i]);
			return 2;
		}
	}

	FILE *fp = fopen(file, "rb");
	int exit_code = read_headers(fp);
	fclose(fp);
	return exit_code;
}
