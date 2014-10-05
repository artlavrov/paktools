#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <direct.h>
#define mkdir _mkdir
#define strdup _strdup
#else
#include <unistd.h>
#include <dirent.h>
#endif

#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>

static int PADDING = 8;

#define SIG_LINK 0
#define SIG_DATA 1

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
	char *p = path;
	char buf[MAX_PATH] = { 0 };

	while (1)
	{
		char *c = strchr(p, '/');
		if (!c)
			c = strchr(p, '\\');
		if (!c)
			break;
		*c = 0;

		strcat(buf, p);
		strcat(buf, "/");

#ifdef _WIN32
		mkdir(buf);
#else
		mkdir(buf, S_IRWXU | S_IRWXG | S_IRWXO);
#endif
		p = c + 1;
	}

	strcat(buf, p);
	FILE *fp = fopen(buf, "wb");
	if (fp)
	{
		fwrite(data, size, 1, fp);
		fclose(fp);
	}

	return 0;
}

int make_sig(char * dest, int type, int align)
{
	char * sig[2][2] = {
		{
			"FILELINK",
			"FILELINK_____END"
		},
		{
			"MANAGEDFILE_DATABLOCK_USED_IN_ENGINE_END",
			"MANAGEDFILE_DATABLOCK_USED_IN_ENGINE_________________________END"
		}
	};
	strcpy (dest, sig[type][align==4?0:1]);
	return strlen(dest);
}

int unpack(const char *fname, const char *dir)
{
	FILE *fp;
	size_t size;

	fp = fopen(fname, "rb");
	if (!fp)
		return -1;

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

	ofs = 8;

	for (int i = 0; i < files; i++)
	{
		fseek(fp, ofs, SEEK_SET);

		char buf[256] = { 0 };
		char sig[256];

		int sig_len = make_sig (sig, SIG_LINK, PADDING);

//		printf("%d %d %s\n", at, size, name);

		fread(buf, sig_len, 1, fp);

		// check if wrong signature
		if (memcmp(buf, sig, sig_len) != 0)
		{
			fclose(fp);
			return -1;
		}

		fread(&at, 4, 1, fp);
		fread(&size, 4, 1, fp);

		char name[MAX_PATH];
		fgets(name, MAX_PATH, fp);
		int len = strlen(name);

		ofs += sig_len + 4*2 + len + 1;
		while (ofs % PADDING != 0)
			ofs++;

		int sig_data_len = make_sig(sig, SIG_DATA, PADDING);
		at += loc + sig_data_len;

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

	fprintf(stderr, "Unpacked %d file(s) into %s\n", files, outdir);

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
	int sig_len;
	char sig[256];
	ofs += 8;
	for (int i = 0; i < files; i++)
	{
		sig_len = make_sig(sig, SIG_LINK, PADDING);

		fwrite(sig, sig_len, 1, fp);
		fwrite(&tmp, 4, 1, fp);
		fwrite(&tmp, 4, 1, fp);

		char name[MAX_PATH];
		fix_path(name, dir, r[i].name);

		fwrite(name, strlen(name) + 1, 1, fp);

		ofs += sig_len + 4 * 2 + strlen(name) + 1;
		while (ofs % PADDING != 0)
			fwrite(&filler, 1, 1, fp), ofs++;
	}

	while (ofs % (PADDING*2) != 0)
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

		char sig[256];
		int sig_data_len = make_sig(sig, SIG_DATA, PADDING);

		fwrite(sig, sig_data_len, 1, fp);
		fwrite(data, size, 1, fp);
		free(data);

		r[i].at = ofs - loc;
		r[i].size = size;

		ofs += sig_data_len + size;
		while (ofs % PADDING != 0)
			fwrite(&filler, 1, 1, fp), ofs++;
	}

	while (ofs % (PADDING*4) != 0)
		fwrite(&filler, 1, 1, fp), ofs++;

	// fixup offsets, etc.
	ofs = 0;
	fseek(fp, ofs, SEEK_SET);
	fwrite(&loc, 4, 1, fp);
	fwrite(&files, 4, 1, fp);
	ofs += 8;

	for (int i = 0; i < files; i++)
	{
		ofs += sig_len;
		fseek(fp, ofs, SEEK_SET);

		fwrite(&r[i].at, 4, 1, fp);
		fwrite(&r[i].size, 4, 1, fp);

		char name[MAX_PATH];
		fix_path(name, dir, r[i].name);

		ofs += 4*2 + strlen(name) + 1;
		while (ofs % PADDING != 0)
			ofs++;

		free(r[i].name);
	}

	fclose(fp);

	fprintf(stderr, "Packed %d file(s) into %s\n", files, path);

	return 0;
}


int main(int argc, char **argv)
{
	char * from = 0;
	char * to = 0;

	int i = 1;

	if (argc>i && !strcmp(argv[i],"-c"))
		PADDING = 4, i++;
	if (argc>i)
		from = argv[i++];
	if (argc>i)
		to = argv[i++];

	if (!from && !to)
	{
		printf("WayForward Engine resource packer (for Duck Tales Remastered, etc.) by artlavrov\n");
		printf("Usage:\n");
		printf("	paktools [options] input.pak [output_dir]\n");
		printf("	paktools [options] input_dir [output.pak]\n");
		printf("Options:\n");
		printf("	-c	compact (BloodRayne) allocation (could be autodetected)\n");
		return 0;
	}

	struct stat st;
	if (stat(from, &st) == -1)
	{
		fprintf(stderr, "Path not found, exiting...\n");
		return -1;
	}

	int res = 0;

	if (((st.st_mode & S_IFDIR) == S_IFDIR))
	{
		res = pack(from, to);
	} else
	{
		res = unpack(from, to);
		if (res<0)
		{
			PADDING = 4;
			res = unpack(from, to);
		}
	}

	if (res<0)
		fprintf(stderr, "Could not open file.");

	return res;
}
