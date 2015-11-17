/*
 * The Qubes OS Project, http://www.qubes-os.org
 *
 * Copyright (c) Invisible Things Lab
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

///////////////////////////////////////////////////////////////////////////////
//
//  FILE: getopt.c
//
//      GetOption function
//
//  FUNCTIONS:
//
//      GetOption() - Get next command line option and parameter
//
//  COMMENTS:
//
///////////////////////////////////////////////////////////////////////////////

#include <stddef.h>
#include <ctype.h>
#include <string.h>

#include "getopt.h"

///////////////////////////////////////////////////////////////////////////////
//
//  FUNCTION: GetOption()
//
//      Get next command line option and parameter
//
//  PARAMETERS:
//
//      argc - count of command line arguments
//      argv - array of command line argument strings
//      pszValidOpts - string of valid, case-sensitive option characters,
//                     a colon ':' following a given character means that
//                     option can take a parameter
//
//  RETURNS:
//
//      If valid option is found, the character value of that option
//          is returned
//      If standalone parameter (with no option) is found, 1 is returned,
//          and optarg points to the standalone parameter
//      If option is found, but it is not in the list of valid options,
//          -1 is returned, and optarg points to the invalid argument
//      When end of argument list is reached, 0 is returned, and
//          optarg is NULL
//
//  COMMENTS:
//
///////////////////////////////////////////////////////////////////////////////

int optind = 1;

char *optargA;
WCHAR *optargW;

WCHAR GetOptionW(
    int argc,
    WCHAR** argv,
    WCHAR* pszValidOpts)
{
    WCHAR chOpt;
    WCHAR* psz = NULL;
    WCHAR* pszParam = NULL;

    if (optind < argc)
    {
        psz = &(argv[optind][0]);
        if (*psz == L'-' || *psz == L'/')
        {
            // we have an option specifier
            chOpt = argv[optind][1];
            if (iswalnum(chOpt) || iswpunct(chOpt))
            {
                // we have an option character
                psz = wcschr(pszValidOpts, chOpt);
                if (psz != NULL)
                {
                    // option is valid, we want to return chOpt
                    if (psz[1] == L':')
                    {
                        // option can have a parameter
                        psz = &(argv[optind][2]);
                        if (*psz == L'\0')
                        {
                            // must look at next argv for param
                            if (optind + 1 < argc)
                            {
                                psz = &(argv[optind + 1][0]);
                                if (*psz == L'-' || *psz == L'/')
                                {
                                    // next argv is a new option, so param
                                    // not given for current option
                                }
                                else
                                {
                                    // next argv is the param
                                    optind++;
                                    pszParam = psz;
                                }
                            }
                            else
                            {
                                // reached end of args looking for param
                            }
                        }
                        else
                        {
                            // param is attached to option
                            pszParam = psz;
                        }
                    }
                    else
                    {
                        // option is alone, has no parameter
                    }
                }
                else
                {
                    // option specified is not in list of valid options
                    chOpt = -1;
                    pszParam = &(argv[optind][0]);
                }
            }
            else
            {
                // though option specifier was given, option character
                // is not alpha or was was not specified
                chOpt = -1;
                pszParam = &(argv[optind][0]);
            }
        }
        else
        {
            // standalone arg given with no option specifier
            chOpt = 1;
            pszParam = &(argv[optind][0]);
        }
    }
    else
    {
        // end of argument list
        chOpt = 0;
    }

    optind++;
    optargW = pszParam;
    return (chOpt);
}

CHAR GetOptionA(
    int argc,
    CHAR** argv,
    CHAR* pszValidOpts)
{
    CHAR chOpt;
    CHAR* psz = NULL;
    CHAR* pszParam = NULL;

    if (optind < argc)
    {
        psz = &(argv[optind][0]);
        if (*psz == '-' || *psz == '/')
        {
            // we have an option specifier
            chOpt = argv[optind][1];
            if (isalnum(chOpt) || ispunct(chOpt))
            {
                // we have an option character
                psz = strchr(pszValidOpts, chOpt);
                if (psz != NULL)
                {
                    // option is valid, we want to return chOpt
                    if (psz[1] == ':')
                    {
                        // option can have a parameter
                        psz = &(argv[optind][2]);
                        if (*psz == '\0')
                        {
                            // must look at next argv for param
                            if (optind + 1 < argc)
                            {
                                psz = &(argv[optind + 1][0]);
                                if (*psz == '-' || *psz == '/')
                                {
                                    // next argv is a new option, so param
                                    // not given for current option
                                }
                                else
                                {
                                    // next argv is the param
                                    optind++;
                                    pszParam = psz;
                                }
                            }
                            else
                            {
                                // reached end of args looking for param
                            }
                        }
                        else
                        {
                            // param is attached to option
                            pszParam = psz;
                        }
                    }
                    else
                    {
                        // option is alone, has no parameter
                    }
                }
                else
                {
                    // option specified is not in list of valid options
                    chOpt = -1;
                    pszParam = &(argv[optind][0]);
                }
            }
            else
            {
                // though option specifier was given, option character
                // is not alpha or was was not specified
                chOpt = -1;
                pszParam = &(argv[optind][0]);
            }
        }
        else
        {
            // standalone arg given with no option specifier
            chOpt = 1;
            pszParam = &(argv[optind][0]);
        }
    }
    else
    {
        // end of argument list
        chOpt = 0;
    }

    optind++;
    optargA = pszParam;
    return (chOpt);
}
