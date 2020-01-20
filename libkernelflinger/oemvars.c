/*
 * Copyright (c) 2014, Intel Corporation
 * All rights reserved.
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

#include <lib.h>

#include "oemvars.h"
#include "vars.h"
#include "text_parser.h"

enum vartype {
	VAR_TYPE_UNKNOWN,
	VAR_TYPE_STRING,
	VAR_TYPE_BLOB
};

typedef struct oemvars_ctx {
	EFI_GUID guid;
	const EFI_GUID *restricted_guid;
	BOOLEAN silent_write_error;
} oemvars_ctx_t;

static BOOLEAN parse_oemvar_guid_line(char *line, EFI_GUID *g)
{
	EFI_STATUS ret;
	const CHAR8 *prefix = (CHAR8 *) "GUID";

	skip_whitespace(&line);

	if (strncmp(prefix, (CHAR8 *)line, strlen(prefix)) != 0)
		return FALSE;

	line += strlen(prefix);
	skip_whitespace(&line);
	if (*line++ != '=')
		return FALSE;
	skip_whitespace(&line);

	ret = stra_to_guid(line, g);
	if (EFI_ERROR(ret))
		return FALSE;

	return TRUE;
}

/* Implements modify-in-place "URL-like" escaping: "%[0-9a-fA-F]{2}"
 * converts to the specified byte; no other modifications are
 * performed (including "+" for space!).  Returns the number of output
 * bytes */
static UINTN unescape_oemvar_val(char *val)
{
	char *p = val, *out = val;
	unsigned int byte;
	char value[3] = { '\0', '\0', '\0' };
	char *tmp;
	while (*p) {
		if (p[0] != '%') {
			*out++ = *p++;
			continue;
		}

		value[0] = p[1];
		value[1] = p[2];
		byte = strtoul(value, &tmp, 16);
		if (tmp == value + 2) {
			*out++ = byte;
			p += 3;
		} else {
			*out++ = *p++;
		}
	}
	*out++ = '\0';
	return out - val;
}

static int parse_oemvar_attributes(char **linep, uint32_t *attributesp, enum vartype *typep)
{
	char *line = *linep;
	char *pos, *end;
	/* No point in writing volatile values. Default to both boot and runtime
	 * access, can remove runtime access with 'b' flag */
	uint32_t attributes = EFI_VARIABLE_NON_VOLATILE |
		EFI_VARIABLE_BOOTSERVICE_ACCESS |
		EFI_VARIABLE_RUNTIME_ACCESS;
	enum vartype type = VAR_TYPE_UNKNOWN;

	/* skip leading whitespace */
	skip_whitespace(&line);

	/* Defaults if no attrs set */
	if (*line != '[')
		goto out;

	line++;
	pos = line;
	end = (char *)strchr((CHAR8 *)line, ']');
	if (!end) {
		error(L"Unclosed attributes specification");
		return -1;
	}
	*end = '\0';
	line = end + 1;

	debug(L"found attributes [%a]", pos);

	while (*pos) {
		switch (*pos) {
		case 'd':
			debug(L"raw data type selected");
			if (type != VAR_TYPE_UNKNOWN) {
				error(L"multiple oem var types specified");
				return -1;
			}
			type = VAR_TYPE_BLOB;
			break;
		case 'b':
			debug(L"restrict to boot services access");
			attributes &= ~EFI_VARIABLE_RUNTIME_ACCESS;
			break;
		case 'a':
			debug(L"time based authenticated variable");
			attributes |= EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS;
			break;
		default:
			error(L"Unknown attribute code '%c'", *pos);
			return -1;
		}
		pos++;
	}

 out:
	if (type == VAR_TYPE_UNKNOWN)
		type = VAR_TYPE_STRING;

	*typep = type;
	*linep = line;
	*attributesp = attributes;

	return 0;
}

static EFI_STATUS parse_line(char *line, VOID *context)
{
	EFI_STATUS ret;
	uint32_t attributes = 0;
	enum vartype type;
	CHAR16 *varname;
	UINTN vallen;
	char  *var, *val, *p;
	oemvars_ctx_t *ctx = (oemvars_ctx_t *)context;

	/* Snip comments */
	if ((p = (char *)strchr((CHAR8 *)line, '#')))
		*p = 0;

	/* GUID line syntax */
	if (parse_oemvar_guid_line(line, &ctx->guid)) {
		debug(L"current guid set to %g", &ctx->guid);
		return EFI_SUCCESS;
	}

	if (ctx->restricted_guid &&
	    memcmp(&ctx->guid, ctx->restricted_guid, sizeof(ctx->guid)))
		return EFI_SUCCESS;

	if (parse_oemvar_attributes(&line, &attributes, &type)) {
		error(L"Invalid attribute specification");
		return EFI_INVALID_PARAMETER;
	}

	/* Variable definition? */
	skip_whitespace(&line);
	var = line;
	val = NULL;
	while (*line && !isspace(*line)) line++;
	if (*line) {
		*line++ = 0;
		skip_whitespace(&line);
		val = line;
	}

	if (!*var)
		return EFI_SUCCESS;

	if (val) {
		switch (type) {
		case VAR_TYPE_BLOB:
			vallen = unescape_oemvar_val(val) - 1;
			break;
		case VAR_TYPE_STRING:
			vallen = unescape_oemvar_val(val);
			break;
		default:
			return EFI_INVALID_PARAMETER;
		}
	} else {
		vallen = 0;
	}

	varname = stra_to_str((CHAR8 *)var);
	if (!varname) {
		error(L"Failed to convert varname string.");
		return EFI_INVALID_PARAMETER;
	}

	if (!memcmp(&ctx->guid, &fastboot_guid, sizeof(ctx->guid))) {
		error(L"fastboot GUID is reserved for Kernelflinger use");
		return EFI_ACCESS_DENIED;
	}

	debug(L"Setting oemvar: %a", var);
	ret = uefi_call_wrapper(RT->SetVariable, 5, varname,
				&ctx->guid, attributes,
				vallen, val);
	FreePool(varname);
	/* Delete a non-existent variable is permitted.  */
	if (EFI_ERROR(ret) && !(ret == EFI_NOT_FOUND && vallen == 0)) {
		if (!ctx->silent_write_error) {
			efi_perror(ret, L"EFI variable setting failed");
			return ret;
		}
		debug(L"EFI variable setting failed: %r", ret);
		debug(L"silent error is on, continue anyway");
	}

	return EFI_SUCCESS;
}

/*
 * GMIN OEM variables are stored as EFI variables. By default, they
 * are under the fastboot GUID.
 *
 * This flash command accepts a text file with a set of OEM variables
 * to set.
 *
 * The syntax supports "#-style" end of line comments. Variable
 * settings are specified as "<var> <val>". Whitespace around the
 * variable name is removed, as is trailing whitespace at the end of
 * the line. The value can otherwise contain any printable character
 * and is stored as an 8-bit string in the EFI variable's
 * value. Non-printable bytes can be encoded with "%xx" URL-style
 * notation.
 *
 * The default attributes set are EFI_VARIABLE_NON_VOLATILE,
 * EFI_VARIABLE_BOOTSERVICE_ACCESS and EFI_VARIABLE_RUNTIME_ACCESS.
 *
 * Prefix argument in the form of "[X]" can be use in front of a
 * variable definition to modify the way the value should be
 * interpreted or the attributes to be used. The currently supported
 * prefix are :
 *
 * - [d]: the value should be stored as raw data and not an 8-bit
 *      string.
 *
 * - [b]: do not set the runtime access attribute.
 *
 * - [a]: set the time based authenticated attribute.
 *
 * A line of the form:
 *
 *   GUID = xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
 *
 * will change the GUID used for subsequent lines.
 */
static EFI_STATUS _flash_oemvars(VOID *data, UINTN size,
				 const EFI_GUID *restricted_guid,
				 BOOLEAN silent_error)
{
	oemvars_ctx_t ctx = {
		.guid = loader_guid,
		.restricted_guid = restricted_guid,
		.silent_write_error = silent_error
	};

	debug(L"Parsing and setting values from oemvars file");
	return parse_text_buffer(data, size, parse_line, &ctx);
}

EFI_STATUS flash_oemvars_silent_write_error(VOID *data, UINTN size,
					    const EFI_GUID *restricted_guid)
{
	return _flash_oemvars(data, size, restricted_guid, TRUE);
}

EFI_STATUS flash_oemvars(VOID *data, UINTN size)
{
	return _flash_oemvars(data, size, NULL, FALSE);
}
