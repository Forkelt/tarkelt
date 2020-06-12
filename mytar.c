#include <stdio.h>
#include <unistd.h>
#include <string.h>

#define EXECUTABLE "mytar"

#define TAR_MAGIC "ustar" /* With null. */
#define TAR_VERSION "00" /* No null. */
#define OLD_MAGIC "ustar  " /* With null. */
#define VERSION_LENGTH 2
#define BLOCK_SIZE 512
#define UNEXPECTED_EOF_CODE -1
#define UNSUPPORTED_TYPE_CODE 2

#define UNKNOWN_OPTION "%s: Unknown option: %s\n"
#define MISSING_OPTIONS "%s: Need at least one option\n"
#define LONE_ZERO_BLOCK "%s: A lone zero block at %d\n"
#define FILE_NOT_FOUND "%s: %s: Not found in archive\n"
#define NOT_FOUND_FINAL "%s: Exiting with failure status due to previous errors\n"
#define UNSUPPORTED_TYPE "%s: Unsupported header type: %d\n"
#define UNEXPECTED_EOF "%s: Unexpected EOF in archive\n%s: Error is not recoverable: exiting now\n"


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
	char pad[12]; /* For a clean 512 bytes; avoids certain issues. */
};


int check_eof(FILE *fp)
{
	int c = fgetc(fp);
	if (feof(fp)) {
		fputc(c, fp);
		return 1;
	}
	fseek(fp, -1, SEEK_CUR);
	fputc(c, fp);
	return 0;
}


int zero_block(void *memory)
{
	size_t i = 0;

	while (i < BLOCK_SIZE)
		if (((char*) memory)[i++])
			return 0;

	return 1;
}


int last_block(FILE *fp, char *prog, int blocks)
{
	char buffer[BLOCK_SIZE];
	if (check_eof(fp))
		fprintf(stderr, LONE_ZERO_BLOCK, prog, blocks + 1);
	else if (!fread(buffer, BLOCK_SIZE, 1, fp))
		return UNEXPECTED_EOF_CODE;
	else if (!zero_block((void*) &buffer))
		fprintf(stderr, LONE_ZERO_BLOCK, prog, blocks + 1);
	
	return 0;
}


int block_align(FILE *fp)
{
	return fseek(fp, (BLOCK_SIZE - ftell(fp) % BLOCK_SIZE) % BLOCK_SIZE, 
		     SEEK_CUR);
}


int read_headers(FILE *fp, char *prog)
{
	struct header head;
	long file_size;
	int blocks;

	for (;;) {
		blocks = ftell(fp) / BLOCK_SIZE;

		if (check_eof(fp)) {
			if (fseek(fp, -1, SEEK_CUR) || check_eof(fp))
				return UNEXPECTED_EOF_CODE;
			return 0;
		}

		if (!fread(&head, sizeof(head), 1, fp))
			return UNEXPECTED_EOF_CODE;

		if (zero_block((void*) &head))
			return last_block(fp, prog, blocks);

		if (head.typeflag && head.typeflag != '0') {
			fprintf(stderr, UNSUPPORTED_TYPE, prog, head.typeflag);
			return UNSUPPORTED_TYPE_CODE;
		}
		
		printf("%s\n", head.name);
		fflush(stdout);

		sscanf(head.size, "%lo", &file_size);
		fseek(fp, file_size, SEEK_CUR);
		block_align(fp);
	}
}


int main(int argc, char *argv[])
{
	char *prog = EXECUTABLE;
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
	int exit_code = read_headers(fp, prog);
	if (exit_code == UNEXPECTED_EOF_CODE) {
		fprintf(stderr, UNEXPECTED_EOF, prog, prog);
		exit_code = 2;
	}
	fclose(fp);
	return exit_code;
}
