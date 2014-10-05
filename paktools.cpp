#include <stdio.h>
#include <string.h>
#include <malloc.h>

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#include <direct.h>
#include <windows.h>
#define mkdir _mkdir
#define getcwd _getcwd
#define chdir _chdir
#define stat _stat
#define strdup _strdup
#else
#include <unistd.h>
#include <dirent.h>
#endif

#include <sys/stat.h>
#include <sys/types.h>

#define SIG_LINK "FILELINK_____END"
#define SIG_DATA "MANAGEDFILE_DATA BLOCK_USED_IN_ENGINE________________________END"

#ifndef MAX_PATH
#define MAX_PATH 4096
#endif

typedef struct {
	int at;
	int size;
	char *name;
} file_t;

int write_to_file(char *path, char *data, int size)
{
	char cwd[MAX_PATH];
	getcwd(cwd, sizeof(cwd));

	char *p = path;
	while (1)
	{
		char *c = strchr(p, '/');
		if (!c)
			c = strchr(p, '\\');
		if (!c)
			break;
		*c = 0;

#ifdef _WIN32
		mkdir(p);
#else
		mkdir(p, S_IRWXU | S_IRWXG | S_IRWXO);
#endif

		chdir(p);
		p = c + 1;
	}

	FILE *fp = fopen(p, "wb");
	if (fp)
	{
		fwrite(data, size, 1, fp);
		fclose(fp);
	}

	chdir(cwd);

	return 0;
}

int unpack(const char *fname, const char *dir)
{
	FILE *fp;
	size_t size;

	fp = fopen(fname, "rb");
	if (!fp)
	{
		fprintf(stderr, "Could not open `%s`, exiting...\n", fname);
		return -1;
	}

	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	int loc, files, ofs, at;

	fread(&loc, 4, 1, fp);
	fread(&files, 4, 1, fp);

	char outdir[MAX_PATH];
	if (!dir)
		sprintf(outdir, "%s_extracted", fname);
	else
		strcpy(outdir, dir);

	fprintf(stderr, "Loaded %s, extracting files to %s...\n", fname, outdir);

	ofs = 8;

	for (int i = 0; i < files; i++)
	{
		fseek(fp, ofs, SEEK_SET);

		char sig[16 + 1] = { 0 };
		fread(sig, 16, 1, fp);	// 16 bytes for FILELINK_____END

		if (memcmp(sig, SIG_LINK, 16) != 0)
		{
			fprintf(stderr, "Not a pak file, exiting...");
			fclose(fp);
			return -1;
		}

		fread(&at, 4, 1, fp);
		fread(&size, 4, 1, fp);

		char name[MAX_PATH];
		fgets(name, MAX_PATH, fp);
		int len = strlen(name);

		at += loc + 64;			// 64 bytes for MANAGEDFILE_DATA BLOCK_USED_IN_ENGINE_________________________END

		ofs += 16 + 4 * 2 + len + 1;
		while (ofs % 8 != 0)
			ofs++;

		char *data = (char *)malloc(size);
		fseek(fp, at, SEEK_SET);
		fread(data, size, 1, fp);


		char *c = strchr(name, ':');
		if (c)
			*c = '/';

		char path[MAX_PATH];
		sprintf(path, "%s/%s", outdir, name);

		write_to_file(path, data, size);

		free(data);
	}

	fclose(fp);

	fprintf(stderr, "Extracted %d files.\n", files);

	return 0;
}

#ifdef _WIN32
int list_dir(char *dir, file_t * files, int i)
{
	HANDLE hf;
	WIN32_FIND_DATA ffd;
	char path[MAX_PATH];
	sprintf(path, "%s/*.*", dir);
	for (hf = FindFirstFile(path, &ffd); FindNextFile(hf, &ffd);)
	{
		if (strcmp(ffd.cFileName, ".") == strcmp(ffd.cFileName, ".."))
		{
			sprintf(path, "%s/%s", dir, ffd.cFileName);
			if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				i = list_dir(path, files, i);
			else
				files[i++].name = strdup(path);
		}
	}
	FindClose(hf);
	return i;
}
#else
int list_dir(char *dir, file_t * files, int i)
{
	char path[MAX_PATH];
	DIR *d;
	struct dirent *dp;
	struct stat st;
	d = opendir(dir);
	if (!d)
		return -1;

	while ((dp = readdir(d)) != NULL)
	{
		if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
			continue;

		sprintf(path, "%s/%s", dir, dp->d_name);
		if (stat(path, &st) == -1)
			return -1;
		if ((st.st_mode & S_IFDIR) == S_IFDIR)
			i = list_dir(path, files, i);
		else
			files[i++].name = strdup(path);
	}

	closedir(d);
	return i;
}
#endif

void fix_path(char *dest, char *dir, char *src)
{
	// example: dir='a/b' src='a/b/c/d/e' => dest='c:d/e'
	char buf[MAX_PATH];
	strcpy(buf, src);

	// set cursor at 'c/d/e'
	char *p = buf + strlen(dir) + 1;

	// make it look like c:d/e
	char *c = strchr(p, '/');
	if (!c)
		c = strchr(p, '\\');
	if (c)
		*c = ':';

	strcpy(dest, p);
}


int pack(char *dir, char *fname)
{
	file_t r[16384];
	int files = list_dir(dir, r, 0);

	if (files <= 0)
	{
		fprintf(stderr, "No files, exiting...\n");
		return -1;
	}

	char path[MAX_PATH];

	if (!fname)
	{
		strcpy(path, dir);
		char *c = strrchr(path, '/');
		if (c)
			*c = 0;
		strcat(path, ".pak");
	}
	else
		strcpy(path, fname);

	fprintf(stderr, "Opening %s, packing file(s)...\n", path);

	FILE *fp = fopen(path, "wb");
	if (!fp)
	{
		fprintf(stderr, "Could not open `%s`, exiting...\n", path);
		return -1;
	}

	int tmp, ofs = 0;
	char filler = 0x3f;
	fwrite(&tmp, 4, 1, fp);
	fwrite(&tmp, 4, 1, fp);

	ofs += 8;
	for (int i = 0; i < files; i++)
	{
		fwrite(SIG_LINK, 16, 1, fp);
		fwrite(&tmp, 4, 1, fp);
		fwrite(&tmp, 4, 1, fp);

		char name[MAX_PATH];
		fix_path(name, dir, r[i].name);

		fwrite(name, strlen(name) + 1, 1, fp);

		ofs += 16 + 4 * 2 + strlen(name) + 1;
		while (ofs % 8 != 0)
			fwrite(&filler, 1, 1, fp), ofs++;
	}

	// fill to 16 bytes (why? idk)
	while (ofs % 16 != 0)
		fwrite(&filler, 1, 1, fp), ofs++;

	int loc = ofs;

	for (int i = 0; i < files; i++)
	{
		// load file into memory
		FILE *in = fopen(r[i].name, "rb");
		fseek(in, 0, SEEK_END);
		int size = ftell(in);
		fseek(in, 0, SEEK_SET);
		char *data = (char *)malloc(size);
		fread(data, size, 1, in);
		fclose(in);
		// loaded

		fwrite(SIG_DATA, 64, 1, fp);
		fwrite(data, size, 1, fp);
		free(data);

		r[i].at = ofs - loc;
		r[i].size = size;

		ofs += 64 + size;
		while (ofs % 16 != 0)
			fwrite(&filler, 1, 1, fp), ofs++;
	}

	while (ofs % 32 != 0)
		fwrite(&filler, 1, 1, fp), ofs++;

	// fixup offsets, etc.
	ofs = 0;
	fseek(fp, ofs, SEEK_SET);
	fwrite(&loc, 4, 1, fp);
	fwrite(&files, 4, 1, fp);
	ofs += 8;

	for (int i = 0; i < files; i++)
	{
		ofs += 16;
		fseek(fp, ofs, SEEK_SET);

		fwrite(&r[i].at, 4, 1, fp);
		fwrite(&r[i].size, 4, 1, fp);

		char name[MAX_PATH];
		fix_path(name, dir, r[i].name);

		ofs += 4 * 2 + strlen(name) + 1;
		while (ofs % 8 != 0)
			ofs++;

		free(r[i].name);
	}

	fclose(fp);

	fprintf(stderr, "Packed %d files.\n", files);

	return 0;
}


int main(int argc, char **argv)
{
	if (argc < 2)
	{
		printf("WayForward Engine resource packer (for Duck Tales Remastered, etc.) by artlavrov\n");
		printf("Usage: paktools input.pak [output_dir]\n");
		printf("       paktools input_dir [output.pak]\n");
		return 0;
	}

	char *from = argv[1];
	char *to = argc > 2 ? argv[2] : 0;

	struct stat st;
	if (stat(from, &st) == -1)
	{
		fprintf(stderr, "Path not found, exiting...\n");
		return -1;
	}

	if ((st.st_mode & S_IFDIR) == S_IFDIR)
		pack(from, to);
	else
		unpack(from, to);

	return 0;
}
