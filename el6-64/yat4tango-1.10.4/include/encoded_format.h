///=============================================================================
//
// file :		encoded_format.h
//
// description :	Include file for the defined Tango::DevEncoded format
//					string
//
// project :		TANGO
//
// author(s) :		E. Taurel
//
// Copyright (C) :      2011,2012
//                      European Synchrotron Radiation Facility
//                      BP 220, Grenoble 38043
//                      FRANCE
//
// This file is part of Tango.
//
// Tango is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Tango is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Tango.  If not, see <http://www.gnu.org/licenses/>.
//
// $Revision: 21911 $
//
// $Log$
//
//
//=============================================================================

#ifndef _ENCODED_FORMAT_H
#define _ENCODED_FORMAT_H

namespace Tango
{

//
// Jpeg encoding
//

#define 		JPEG_GRAY_8		"JPEG_GRAY8"
#define			JPEG_RGB  		"JPEG_RGB"

//
// Gray encoding
//

#define			GRAY_8			"GRAY8"
#define			GRAY_16			"GRAY16"

//
// RGB
//

#define			RGB_24			"RGB24"

} // End of Tango namespace

#endif // _ENCODED_FORMAT_H
