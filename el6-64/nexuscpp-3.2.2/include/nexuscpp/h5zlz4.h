/*
   License for Dectris lz4-hdf5 filter plugin
   Copyright (C) 2011-2013, Dectris Ltd.
   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   You can contact the author at :
   Dectris homepage : http://www.dectris.com
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <H5PLextern.h>

#define LZ4_FILTER 32004

size_t lz4_filter(unsigned int flags, size_t cd_nelmts,  
			 const unsigned int cd_values[], size_t nbytes,
			 size_t *buf_size, void **buf);

const H5Z_class2_t H5Z_LZ4[1] = {{
    H5Z_CLASS_T_VERS,                 /* H5Z_class_t version */
    (H5Z_filter_t)LZ4_FILTER,         /* Filter id number             */
    1,              /* encoder_present flag (set to true) */
    1,              /* decoder_present flag (set to true) */
    "HDF5 lz4 filter; see http://www.hdfgroup.org/services/contributions.html", 
                    /* Filter name for debugging    */
    NULL,           /* The "can apply" callback     */
    NULL,           /* The "set local" callback     */
    (H5Z_func_t)lz4_filter,         /* The actual filter function   */
  }};
