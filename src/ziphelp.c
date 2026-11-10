/*
 * s16-pi - ROMs out of a MAME zip held in memory, PNG screenshots (miniz)
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "miniz.h"

/* one ROM, by CRC and size first (as MAME), then by name; returns 1 if found */
int s16pi_zip_fetch (const void *pZip, size_t nZipSize, const char *pName, uint32_t nCRC,
		     uint32_t nSize, uint8_t *pDst)
{
	if (pZip == 0)
	{
		return 0;
	}
	mz_zip_archive zip;
	memset (&zip, 0, sizeof zip);
	if (!mz_zip_reader_init_mem (&zip, pZip, nZipSize, 0))
	{
		return 0;
	}
	int nFound = -1;
	mz_uint nFiles = mz_zip_reader_get_num_files (&zip);
	for (mz_uint i = 0; i < nFiles && nFound < 0; i++)
	{
		mz_zip_archive_file_stat st;
		if (mz_zip_reader_file_stat (&zip, i, &st) && st.m_crc32 == nCRC && st.m_uncomp_size == nSize)
		{
			nFound = (int) i;
		}
	}
	if (nFound < 0)
	{
		nFound = mz_zip_reader_locate_file (&zip, pName, NULL, 0);
	}
	int bOK = 0;
	if (nFound >= 0)
	{
		mz_zip_archive_file_stat st;
		bOK =    mz_zip_reader_file_stat (&zip, (mz_uint) nFound, &st)
		      && st.m_uncomp_size == nSize
		      && mz_zip_reader_extract_to_mem (&zip, (mz_uint) nFound, pDst, nSize, 0);
	}
	mz_zip_reader_end (&zip);
	return bOK;
}

/* PNG of an RGB image (3 bytes per pixel); free the result with s16pi_png_free */
void *s16pi_png_encode (const void *pRGB, int nWidth, int nHeight, size_t *pSize)
{
	return tdefl_write_image_to_png_file_in_memory_ex (pRGB, nWidth, nHeight, 3, pSize,
							    6, MZ_FALSE);
}

void s16pi_png_free (void *p)
{
	mz_free (p);
}
