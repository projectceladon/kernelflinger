/*
 * Copyright (c) 2014, Intel Corporation
 * All rights reserved.
 *
 * Authors: Jeremy Compostella <jeremy.compostella@intel.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer
 *      in the documentation and/or other materials provided with the
 *      distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <png.h>
#include <zlib.h>
#include <string.h>
#include <libgen.h>
#include <getopt.h>
#include <errno.h>
#include <stdbool.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*x))

static char *program_name;

static void usage(int status)
{
	printf("Usage: %s -i FILE -o FILE -f FORMAT -p NAME\n",
	       basename((char *)program_name));
	printf("\
Transform PNG file to C source data structure.\n\
  -o, --output-file=FILE        write data into FILE instead of printing it\n\
  -i, --input-file=FILE         write data into FILE instead of printing it\n\
  -f, --output-format=FORMAT    allowed values are: RGBA, BGRA, GRAY\n\
  -p, --prefix=NAME             prefix name for C content\n\
  -h, --help                    display this help\n\
");
	exit(status);
}

static void error(const char *s)
{
	perrors(s);
	exit(EXIT_FAILURE);
}

static const unsigned int LINE_LENGTH = 80;

static void write_to_c_source(const char *name, png_bytep buffer,
			     unsigned int size, const char *path)
{
	unsigned int i, col;
	const unsigned int item_len = strlen("0x00, ");
	FILE *f;
	bool free_f = false;

	if (!strcmp(path, "-"))
		f = stdout;
	else {
		f = fopen(path, "w");
		free_f = true;
		if (!f)
			error("Failed to create output file.");
	}

	fprintf(f, "unsigned char %s_dat[] = {", name);
	for (i = 0, col = 2; i < size; i++, col += item_len, buffer++) {
		if (col >= LINE_LENGTH - item_len)
			col = 2;
		fprintf(f, "%s0x%02x%s", col == 2 ? "\n  " : " ",
			*buffer, i != size - 1 ? "," : "");
	}
	fprintf(f, "\n};\n");
	fprintf(f, "unsigned int %s_dat_len = %d;\n", name, size);
	if (free_f)
		fclose(f);
}

static png_uint_32 get_format_from_string(const char *str)
{
	static struct str_to_format {
		const char *str;
		png_uint_32 format;
	} formats[] = {
		{ "RGBA", PNG_FORMAT_RGBA },
		{ "BGRA", PNG_FORMAT_BGRA },
		{ "GRAY", PNG_FORMAT_GRAY }
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(formats); i++)
		if (!strcmp(str, formats[i].str))
			return formats[i].format;

	usage(EXIT_FAILURE);
	return 0;
}

static struct option const long_options[] = {
	{"input-file", required_argument, NULL, 'i'},
	{"output-file", required_argument, NULL, 'o'},
	{"output-format", required_argument, NULL, 'f'},
	{"prefix", required_argument, NULL, 'p'},
	{"help", no_argument, NULL, 'h'},
	{NULL, 0, NULL, 0}
};

int main(int argc, char **argv)
{
	png_image image;
	png_bytep buffer;
	unsigned int size;
	bool format_initialized = false;
	png_uint_32 format = 0;
	const char *ipath = NULL;
	const char *opath = NULL;
	const char *prefix = NULL;
	char c;

	program_name = argv[0];

	while ((c = getopt_long(argc, argv, "i:o:f:p:h", long_options, NULL)) != -1) {
		switch (c) {
		case 'i':
			ipath = optarg;
			break;
		case 'o':
			opath = optarg;
			break;
		case 'p':
			prefix = optarg;
			break;
		case 'f':
			format = get_format_from_string(optarg);
			format_initialized = true;
			break;
		case 'h':
			usage(EXIT_SUCCESS);
			break;
		default:
			usage(EXIT_FAILURE);
			break;
		}
	}

	if (!format_initialized || !opath || !ipath || !prefix)
		usage(EXIT_FAILURE);

	memset(&image, 0, sizeof(image));
	image.version = PNG_IMAGE_VERSION;

	if (!png_image_begin_read_from_file(&image, ipath))
		error("Failed to open PNG file.");

	image.format = format;
	size = PNG_IMAGE_SIZE(image);

	buffer = malloc(size);
	if (!buffer)
		error("Failed to allocate buffer.");

	if (!png_image_finish_read(&image, NULL, buffer, 0, NULL))
		error("Failed to read  PNG file.");

	write_to_c_source(prefix, buffer, size, opath);

	png_image_free(&image);
	free(buffer);

	return EXIT_SUCCESS;
}
