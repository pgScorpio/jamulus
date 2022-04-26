#pragma once

/******************************************************************************\
 * Copyright (c) 2022
 *
 * Author(s):
 *  Peter Goderie (pgScorpio)
 *
 ******************************************************************************
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 *
\******************************************************************************/

#include "cmdline.h"
#include <float.h>

/******************************************************************************
 CmdlnOptions classes:
 ******************************************************************************/

enum class ECmdlnOptDestType
{
    Invalid = 0,
    Client  = 1,
    Server  = 2,
    Common  = 3,

    Main = 128 // help, version
};

enum class ECmdlnOptValType
{
    None   = 0,
    Flag   = 1,
    String = 2,
    // Number
    Double = 3,
    UInt   = 4,
    Int    = 5
};

enum class ECmdlnOptCheckResult
{
    InvalidLength = -6, // strValue is truncated (CCmndlnStringOption::check result only)

    // CCmdlnOptBase::check results
    InvalidDest = -5, // parameter Ok, but not applicable for destType(s)
                      // (strParam and strValue will be set, dblValue is set only if it was a number parameter)

    InvalidRange  = -4, // number is out of range
    InvalidNumber = -3, // parameteris not a number as expected
    NoValue       = -2, // missing value (end of commandline after parameter)
    NoParam       = -1, // end of commandline
    NoMatch       = 0,  // no parameter name match

    OkFlag   = 1, // name match, flag parameter
    OkString = 2, // name match, String parameter
    OkNumber = 3  // name match, Ok Number parameter
};

/******************************************************************************
 CCmdlnOptBase: base class for all commandline option classes
 ******************************************************************************/

class CCmdlnOptBase
{
public:
    CCmdlnOptBase ( ECmdlnOptValType valueType, ECmdlnOptDestType destType, const QString& shortName, const QString& longName ) :
        bSet ( false ),
        eValueType ( valueType ),
        eDestType ( destType ),
        strShortName ( shortName ),
        strLongName ( longName ),
        pDepricated ( NULL )
    {}

    inline bool IsDepricated() { return ( pDepricated != NULL ); };

protected:
    friend class CCommandlineOptions;

    CCmdlnOptBase*          pDepricated;
    bool                    bSet;
    const ECmdlnOptValType  eValueType;
    const ECmdlnOptDestType eDestType;
    const QString           strShortName;
    const QString           strLongName;

protected:
    ECmdlnOptCheckResult doCheck ( ECmdlnOptDestType destType,
                                   int               argc,
                                   char**            argv,
                                   int&              i,
                                   QString&          strParam,
                                   QString&          strValue,
                                   double&           dblValue );

    void Set()
    {
        bSet = true;
        if ( pDepricated )
        {
            pDepricated->bSet = true;
        }
    }

    void Unset() { bSet = false; }

public:
    inline void SetDepricated ( CCmdlnOptBase& dep ) { pDepricated = &dep; }

    inline bool IsSet() const { return bSet; }

    virtual ECmdlnOptCheckResult check ( ECmdlnOptDestType destType, int argc, char** argv, int& i, QString& strParam, QString& strValue ) = 0;
};

/******************************************************************************
 CCmndlnFlagOption: Defines a Flag commandline option
 ******************************************************************************/

class CCmndlnFlagOption : public CCmdlnOptBase
{
public:
    CCmndlnFlagOption ( ECmdlnOptDestType destType, const QString& shortName, const QString& longName ) :
        CCmdlnOptBase ( ECmdlnOptValType::Flag, destType, shortName, longName )
    {}

    inline CCmndlnFlagOption& Depricated() { return pDepricated ? *dynamic_cast<CCmndlnFlagOption*> ( pDepricated ) : *this; };

protected:
    friend class CCommandlineOptions;

    inline void SetDepricated ( CCmndlnFlagOption& dep ) { pDepricated = &dep; }

public:
    virtual ECmdlnOptCheckResult check ( ECmdlnOptDestType destType, int argc, char** argv, int& i, QString& strParam, QString& strValue ) override;
};

/******************************************************************************
 CCmndlnStringOption: Defines a String commandline option
 ******************************************************************************/

class CCmndlnStringOption : public CCmdlnOptBase
{
public:
    CCmndlnStringOption ( ECmdlnOptDestType destType,
                          const QString&    shortName,
                          const QString&    longName,
                          const QString     defVal = "",
                          int               maxLen = -1 ) :
        CCmdlnOptBase ( ECmdlnOptValType::String, destType, shortName, longName ),
        iMaxLen ( maxLen ),
        strValue ( defVal )
    {}

    void Clear()
    {
        strValue.clear();
        bSet = false;
    }

    inline const QString& Value() const { return strValue; }

    inline CCmndlnStringOption& Depricated() { return pDepricated ? *dynamic_cast<CCmndlnStringOption*> ( pDepricated ) : *this; };

protected:
    friend class CCommandlineOptions;

    int     iMaxLen;
    QString strValue;

    inline void SetDepricated ( CCmndlnStringOption& dep ) { pDepricated = &dep; }

    bool Set ( QString val )
    {
        bool res = true;

        if ( pDepricated )
        {
            res      = static_cast<CCmndlnStringOption*> ( pDepricated )->Set ( val );
            strValue = static_cast<CCmndlnStringOption*> ( pDepricated )->strValue;
        }
        else
        {
            strValue = val;

            if ( strValue > iMaxLen )
            {
                strValue.truncate ( iMaxLen );
                res = false;
            }
        }

        bSet = true;

        return res;
    }

public:
    virtual ECmdlnOptCheckResult check ( ECmdlnOptDestType destType, int argc, char** argv, int& i, QString& strParam, QString& strValue ) override;
};

/******************************************************************************
 CCmndlnDoubleOption: Defines a double commandline option
 ******************************************************************************/

class CCmndlnDoubleOption : public CCmdlnOptBase
{
public:
    CCmndlnDoubleOption ( ECmdlnOptDestType destType,
                          const QString&    shortName,
                          const QString&    longName,
                          double            defVal = 0,
                          double            min    = DBL_MIN,
                          double            max    = DBL_MAX ) :
        CCmdlnOptBase ( ECmdlnOptValType::Double, destType, shortName, longName ),
        dValue ( defVal ),
        dMin ( min ),
        dMax ( max )
    {}

    inline double Value() { return dValue; };

    inline CCmndlnDoubleOption& Depricated() { return pDepricated ? *dynamic_cast<CCmndlnDoubleOption*> ( pDepricated ) : *this; };

protected:
    friend class CCommandlineOptions;

    double dValue;
    double dMin;
    double dMax;

    inline void SetDepricated ( CCmndlnDoubleOption& dep ) { pDepricated = &dep; }

    bool Set ( double val )
    {
        bool res = true;

        if ( pDepricated )
        {
            res    = static_cast<CCmndlnDoubleOption*> ( pDepricated )->Set ( val );
            dValue = static_cast<CCmndlnDoubleOption*> ( pDepricated )->dValue;
        }
        else
        {
            dValue = val;
            bSet   = true;

            if ( dValue < dMin )
            {
                dValue = dMin;
                res    = false;
            }
            else if ( dValue > dMax )
            {
                dValue = dMax;
                res    = false;
            }
        }

        bSet = true;

        return res;
    }

public:
    virtual ECmdlnOptCheckResult check ( ECmdlnOptDestType destType, int argc, char** argv, int& i, QString& strParam, QString& strValue ) override;
};

/******************************************************************************
 CCmndlnIntOption: Defines an integer commandline option
 ******************************************************************************/

class CCmndlnIntOption : public CCmdlnOptBase
{
public:
    CCmndlnIntOption ( ECmdlnOptDestType destType,
                       const QString&    shortName,
                       const QString&    longName,
                       int               defVal = 0,
                       int               min    = INT_MIN,
                       int               max    = INT_MAX ) :
        CCmdlnOptBase ( ECmdlnOptValType::Int, destType, shortName, longName ),
        iValue ( defVal ),
        iMin ( min ),
        iMax ( max )
    {}

    inline unsigned int Value() { return iValue; };

    inline CCmndlnIntOption& Depricated() { return pDepricated ? *dynamic_cast<CCmndlnIntOption*> ( pDepricated ) : *this; };

protected:
    friend class CCommandlineOptions;

    int iValue;
    int iMin;
    int iMax;

    inline void SetDepricated ( CCmndlnIntOption& dep ) { pDepricated = &dep; }

    bool Set ( int val )
    {
        bool res = true;

        if ( pDepricated )
        {
            res    = static_cast<CCmndlnIntOption*> ( pDepricated )->Set ( val );
            iValue = static_cast<CCmndlnIntOption*> ( pDepricated )->iValue;
        }
        else
        {
            iValue = val;
            bSet   = true;

            if ( iValue < iMin )
            {
                iValue = iMin;
                res    = false;
            }
            else if ( iValue > iMax )
            {
                iValue = iMax;
                res    = false;
            }
        }

        bSet = true;

        return res;
    }

public:
    virtual ECmdlnOptCheckResult check ( ECmdlnOptDestType destType, int argc, char** argv, int& i, QString& strParam, QString& strValue ) override;
};

/******************************************************************************
 CCmndlnUIntOption: Defines a unsigned int commandline option
 ******************************************************************************/

class CCmndlnUIntOption : public CCmdlnOptBase
{
public:
    CCmndlnUIntOption ( ECmdlnOptDestType destType,
                        const QString&    shortName,
                        const QString&    longName,
                        unsigned int      defVal = 0,
                        unsigned int      min    = 0,
                        unsigned int      max    = UINT_MAX ) :
        CCmdlnOptBase ( ECmdlnOptValType::UInt, destType, shortName, longName ),
        uiValue ( defVal ),
        uiMin ( min ),
        uiMax ( max )
    {}

    inline unsigned int Value() { return uiValue; };

    inline CCmndlnUIntOption& Depricated() { return pDepricated ? *dynamic_cast<CCmndlnUIntOption*> ( pDepricated ) : *this; };

protected:
    friend class CCommandlineOptions;

    unsigned int uiValue;
    unsigned int uiMin;
    unsigned int uiMax;

    inline void SetDepricated ( CCmndlnUIntOption& dep ) { pDepricated = &dep; }

    bool Set ( unsigned int val )
    {
        bool res = true;

        if ( pDepricated )
        {
            res     = static_cast<CCmndlnUIntOption*> ( pDepricated )->Set ( val );
            uiValue = static_cast<CCmndlnUIntOption*> ( pDepricated )->uiValue;
        }
        else
        {
            uiValue = val;
            bSet    = true;

            if ( uiValue < uiMin )
            {
                uiValue = uiMin;
                res     = false;
            }
            else if ( uiValue > uiMax )
            {
                uiValue = uiMax;
                res     = false;
            }
        }

        bSet = true;

        return res;
    }

public:
    virtual ECmdlnOptCheckResult check ( ECmdlnOptDestType destType, int argc, char** argv, int& i, QString& strParam, QString& strValue ) override;
};

class CCommandlineOptions
{
public:
    CCommandlineOptions();

    // Load: Parses commandline and sets all given options.
    bool Load ( bool bIsClient, bool bUseGUI, int argc, char** argv );

protected:
    // check: Called from Load
    //        Checks the Load result and shows any errors.
    bool CCommandlineOptions::check ( ECmdlnOptDestType eDestType,
                                      const QString&    unknowOptions,
                                      const QString&    invalidDests,
                                      const QString&    invalidParams,
                                      const QString&    depricatedParams );

public:
    // NOTE: when adding commandline options:
    //       first add short/long option name definitions in cmdline.h
    //       then add the appropriate option class here (name should be the same as the option long name)
    //       init the option class in the CCommandlineOptions constructor
    //       add the option to the array in the CCommandlineOptions::Load function

    // NOTE: server and nogui are read seperately in main. main corrects them depending on build configurarion
    //       and passes bIsClientand bUseGui to the Load function which will adjust server and nogui accordingly

    // NOTE: since the server option itself is already checked in main and, if it is present,
    //       CommandlineOptions are loaded as Server, as so --server is a server-only option here

    // NOTE: help and version are not in this list, since they are handled in main
    //       and CommandlineOptions are never loaded when any of those are on the commandline
    //       since main will exit before that.

    // Common options:
    CCmndlnStringOption inifile;
    CCmndlnFlagOption   nogui;
    CCmndlnUIntOption   port;
    CCmndlnUIntOption   jsonrpcport;
    CCmndlnStringOption jsonrpcsecretfile;
    CCmndlnUIntOption   qos;
    CCmndlnFlagOption   notranslation;
    CCmndlnFlagOption   enableipv6;

    // Client Only options:
    CCmndlnStringOption connect;
    CCmndlnFlagOption   nojackconnect;
    CCmndlnFlagOption   mutestream;
    CCmndlnFlagOption   mutemyown;
    CCmndlnStringOption clientname;
    CCmndlnStringOption ctrlmidich;
    CCmndlnStringOption centralserver;

    CCmndlnFlagOption showallservers;
    CCmndlnFlagOption showanalyzerconsole;

    // Server Only options:
    CCmndlnFlagOption   server;
    CCmndlnFlagOption   discononquit;
    CCmndlnStringOption directoryserver;
    CCmndlnStringOption directoryfile;
    CCmndlnStringOption listfilter;
    CCmndlnFlagOption   fastupdate;
    CCmndlnStringOption log;
    CCmndlnFlagOption   licence;
    CCmndlnStringOption htmlstatus;
    CCmndlnStringOption serverinfo;
    CCmndlnStringOption serverpublicip;
    CCmndlnFlagOption   delaypan;
    CCmndlnStringOption recording;
    CCmndlnFlagOption   norecord;
    CCmndlnStringOption serverbindip;
    CCmndlnFlagOption   multithreading;
    CCmndlnUIntOption   numchannels;
    CCmndlnStringOption welcomemessage;
    CCmndlnFlagOption   startminimized;

    CCmndlnFlagOption special;
};