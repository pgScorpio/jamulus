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

#include "messages.h"

tMainform* CMessages::pMainForm = NULL;
QString    CMessages::strMainFormName;

/******************************************************************************\
* Message Boxes                                                                *
\******************************************************************************/
void CMessages::ShowError ( QString strError )
{
#ifndef HEADLESS
    QMessageBox::critical ( pMainForm, strMainFormName + ": " + QObject::tr ( "Error" ), strError, QObject::tr ( "Ok" ), nullptr );
#else
    qCritical() << "Error: " << strError.toLocal8Bit().data();
#endif
}

void CMessages::ShowWarning ( QString strWarning )
{
#ifndef HEADLESS
    QMessageBox::warning ( pMainForm, strMainFormName + ": " + QObject::tr ( "Warning" ), strWarning, QObject::tr ( "Ok" ), nullptr );
#else
    qWarning() << "Warning: " << strWarning.toLocal8Bit().data();
#endif
}

void CMessages::ShowInfo ( QString strInfo )
{
#ifndef HEADLESS
    QMessageBox::information ( pMainForm, strMainFormName + ": " + QObject::tr ( "Information" ), strInfo, QObject::tr ( "Ok" ), nullptr );
#else
    qInfo() << "Info: " << strInfo.toLocal8Bit().data();
#endif
}