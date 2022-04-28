/******************************************************************************\
 * Copyright (c) 2004-2022
 *
 * Author(s):
 *  Volker Fischer
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

#include "client.h"

/* Implementation *************************************************************/
CClient::CClient ( CClientSettings& cSettings ) :
    Settings ( cSettings ),
    Channel ( false ), /* we need a client channel -> "false" */
    CurOpusEncoder ( nullptr ),
    CurOpusDecoder ( nullptr ),
    eAudioCompressionType ( CT_OPUS ),
    iCeltNumCodedBytes ( OPUS_NUM_BYTES_MONO_LOW_QUALITY ),
    iOPUSFrameSizeSamples ( DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES ),
    iNumAudioChannels ( 1 ),
    bIsInitializationPhase ( true ),
    fMuteOutStreamGain ( 1.0f ),
    Socket ( &Channel,
             cSettings.CommandlineOptions.port.Value(),
             cSettings.CommandlineOptions.qos.Value(),
             "",
             cSettings.CommandlineOptions.enableipv6.IsSet() ),
    //### TODO: BEGIN//
    // pass Settings to CCsound too
    Sound ( AudioCallback,
            this,
            cSettings.CommandlineOptions.ctrlmidich.Value(),
            cSettings.CommandlineOptions.nojackconnect.IsSet(),
            cSettings.CommandlineOptions.clientname.Value() ),
    //### TODO: END//
    iSndCrdPrefFrameSizeFactor ( FRAME_SIZE_FACTOR_DEFAULT ),
    iSndCrdFrameSizeFactor ( FRAME_SIZE_FACTOR_DEFAULT ),
    bSndCrdConversionBufferRequired ( false ),
    iSndCardMonoBlockSizeSamConvBuff ( 0 ),
    bJitterBufferOK ( true ),
    bEnableIPv6 ( cSettings.CommandlineOptions.enableipv6.IsSet() ),
    bMuteMeInPersonalMix ( cSettings.CommandlineOptions.mutemyown.IsSet() ),
    pSignalHandler ( CSignalHandler::getSingletonP() )
{
    Settings.SetClientName ( cSettings.CommandlineOptions.clientname.Value() );

    int iOpusError;

    OpusMode = opus_custom_mode_create ( SYSTEM_SAMPLE_RATE_HZ, DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES, &iOpusError );

    Opus64Mode = opus_custom_mode_create ( SYSTEM_SAMPLE_RATE_HZ, SYSTEM_FRAME_SIZE_SAMPLES, &iOpusError );

    // init audio encoders and decoders
    OpusEncoderMono     = opus_custom_encoder_create ( OpusMode, 1, &iOpusError );   // mono encoder legacy
    OpusDecoderMono     = opus_custom_decoder_create ( OpusMode, 1, &iOpusError );   // mono decoder legacy
    OpusEncoderStereo   = opus_custom_encoder_create ( OpusMode, 2, &iOpusError );   // stereo encoder legacy
    OpusDecoderStereo   = opus_custom_decoder_create ( OpusMode, 2, &iOpusError );   // stereo decoder legacy
    Opus64EncoderMono   = opus_custom_encoder_create ( Opus64Mode, 1, &iOpusError ); // mono encoder OPUS64
    Opus64DecoderMono   = opus_custom_decoder_create ( Opus64Mode, 1, &iOpusError ); // mono decoder OPUS64
    Opus64EncoderStereo = opus_custom_encoder_create ( Opus64Mode, 2, &iOpusError ); // stereo encoder OPUS64
    Opus64DecoderStereo = opus_custom_decoder_create ( Opus64Mode, 2, &iOpusError ); // stereo decoder OPUS64

    // we require a constant bit rate
    opus_custom_encoder_ctl ( OpusEncoderMono, OPUS_SET_VBR ( 0 ) );
    opus_custom_encoder_ctl ( OpusEncoderStereo, OPUS_SET_VBR ( 0 ) );
    opus_custom_encoder_ctl ( Opus64EncoderMono, OPUS_SET_VBR ( 0 ) );
    opus_custom_encoder_ctl ( Opus64EncoderStereo, OPUS_SET_VBR ( 0 ) );

    // for 64 samples frame size we have to adjust the PLC behavior to avoid loud artifacts
    opus_custom_encoder_ctl ( Opus64EncoderMono, OPUS_SET_PACKET_LOSS_PERC ( 35 ) );
    opus_custom_encoder_ctl ( Opus64EncoderStereo, OPUS_SET_PACKET_LOSS_PERC ( 35 ) );

    // we want as low delay as possible
    opus_custom_encoder_ctl ( OpusEncoderMono, OPUS_SET_APPLICATION ( OPUS_APPLICATION_RESTRICTED_LOWDELAY ) );
    opus_custom_encoder_ctl ( OpusEncoderStereo, OPUS_SET_APPLICATION ( OPUS_APPLICATION_RESTRICTED_LOWDELAY ) );
    opus_custom_encoder_ctl ( Opus64EncoderMono, OPUS_SET_APPLICATION ( OPUS_APPLICATION_RESTRICTED_LOWDELAY ) );
    opus_custom_encoder_ctl ( Opus64EncoderStereo, OPUS_SET_APPLICATION ( OPUS_APPLICATION_RESTRICTED_LOWDELAY ) );

    // set encoder low complexity for legacy 128 samples frame size
    opus_custom_encoder_ctl ( OpusEncoderMono, OPUS_SET_COMPLEXITY ( 1 ) );
    opus_custom_encoder_ctl ( OpusEncoderStereo, OPUS_SET_COMPLEXITY ( 1 ) );

    // Connections -------------------------------------------------------------
    // connections for the protocol mechanism
    QObject::connect ( &Channel, &CChannel::MessReadyForSending, this, &CClient::OnSendProtMessage );
    QObject::connect ( &Channel, &CChannel::DetectedCLMessage, this, &CClient::OnDetectedCLMessage );
    QObject::connect ( &Channel, &CChannel::ReqJittBufSize, this, &CClient::OnReqJittBufSize );
    QObject::connect ( &Channel, &CChannel::JittBufSizeChanged, this, &CClient::OnServerJittBufSizeChanged );
    QObject::connect ( &Channel, &CChannel::ReqChanInfo, this, &CClient::OnChannelInfoChanged );
    QObject::connect ( &Channel, &CChannel::ConClientListMesReceived, this, &CClient::ConClientListMesReceived );
    QObject::connect ( &Channel, &CChannel::NewConnection, this, &CClient::OnConnected );
    QObject::connect ( &Channel, &CChannel::Disconnected, this, &CClient::OnDisconnected );
    QObject::connect ( &Channel, &CChannel::ChatTextReceived, this, &CClient::ChatTextReceived );
    QObject::connect ( &Channel, &CChannel::ClientIDReceived, this, &CClient::OnClientIDReceived );
    QObject::connect ( &Channel, &CChannel::MuteStateHasChangedReceived, this, &CClient::MuteStateHasChangedReceived );
    QObject::connect ( &Channel, &CChannel::LicenceRequired, this, &CClient::LicenceRequired );
    QObject::connect ( &Channel, &CChannel::VersionAndOSReceived, this, &CClient::VersionAndOSReceived );
    QObject::connect ( &Channel, &CChannel::RecorderStateReceived, this, &CClient::RecorderStateReceived );

    QObject::connect ( &ConnLessProtocol, &CProtocol::CLMessReadyForSending, this, &CClient::OnSendCLProtMessage );
    QObject::connect ( &ConnLessProtocol, &CProtocol::CLServerListReceived, this, &CClient::CLServerListReceived );
    QObject::connect ( &ConnLessProtocol, &CProtocol::CLRedServerListReceived, this, &CClient::CLRedServerListReceived );
    QObject::connect ( &ConnLessProtocol, &CProtocol::CLConnClientsListMesReceived, this, &CClient::CLConnClientsListMesReceived );
    QObject::connect ( &ConnLessProtocol, &CProtocol::CLPingReceived, this, &CClient::OnCLPingReceived );
    QObject::connect ( &ConnLessProtocol, &CProtocol::CLPingWithNumClientsReceived, this, &CClient::OnCLPingWithNumClientsReceived );
    QObject::connect ( &ConnLessProtocol, &CProtocol::CLDisconnection, this, &CClient::OnCLDisconnection );
    QObject::connect ( &ConnLessProtocol, &CProtocol::CLVersionAndOSReceived, this, &CClient::CLVersionAndOSReceived );
    QObject::connect ( &ConnLessProtocol, &CProtocol::CLChannelLevelListReceived, this, &CClient::CLChannelLevelListReceived );

    // other
    QObject::connect ( &Sound, &CSound::ReinitRequest, this, &CClient::OnSndCrdReinitRequest );
    QObject::connect ( &Sound, &CSound::ControllerInFaderLevel, this, &CClient::OnControllerInFaderLevel );
    QObject::connect ( &Sound, &CSound::ControllerInPanValue, this, &CClient::OnControllerInPanValue );
    QObject::connect ( &Sound, &CSound::ControllerInFaderIsSolo, this, &CClient::OnControllerInFaderIsSolo );
    QObject::connect ( &Sound, &CSound::ControllerInFaderIsMute, this, &CClient::OnControllerInFaderIsMute );
    QObject::connect ( &Sound, &CSound::ControllerInMuteMyself, this, &CClient::OnControllerInMuteMyself );

    QObject::connect ( &Settings, &CClientSettings::ConnectRequest, this, &CClient::OnConnectRequest );
    QObject::connect ( &Settings, &CClientSettings::DisconnectRequest, this, &CClient::OnDisconnectRequest );

    QObject::connect ( &Settings, &CClientSettings::AudioDeviceChanged, this, &CClient::OnAudioDeviceChanged );
    QObject::connect ( &Settings, &CClientSettings::InputChannelChanged, this, &CClient::OnInputChannelChanged );
    QObject::connect ( &Settings, &CClientSettings::OutputChannelChanged, this, &CClient::OnOutputChannelChanged );
    QObject::connect ( &Settings, &CClientSettings::PrefFrameSizeFactorChanged, this, &CClient::OnPrefFrameSizeFactorChanged );
    QObject::connect ( &Settings, &CClientSettings::OpenDriverSetup, this, &CClient::OnDriverSetup );
    QObject::connect ( &Settings, &CClientSettings::AudioChannelConfigChanged, this, &CClient::OnReInitRequest );
    QObject::connect ( &Settings, &CClientSettings::AudioQualityChanged, this, &CClient::OnReInitRequest );
    QObject::connect ( &Settings, &CClientSettings::EnableOPUS64Changed, this, &CClient::OnReInitRequest );
    QObject::connect ( &Settings, &CClientSettings::ClientSockBufNumFramesChanged, this, &CClient::OnClientSockBufNumFramesChanged );
    QObject::connect ( &Settings, &CClientSettings::ServerSockBufNumFramesChanged, this, &CClient::OnServerSockBufNumFramesChanged );
    QObject::connect ( &Settings, &CClientSettings::AutoSockBufSizeChanged, this, &CClient::OnAutoSockBufSizeChanged );
    QObject::connect ( &Settings, &CClientSettings::ChannelInfoChanged, this, &CClient::OnChannelInfoChanged );
    QObject::connect ( &Settings, &CClientSettings::ReverbChannelChanged, this, &CClient::OnReverbChannelChanged );

    QObject::connect ( &Socket, &CHighPrioSocket::InvalidPacketReceived, this, &CClient::OnInvalidPacketReceived );

    QObject::connect ( pSignalHandler, &CSignalHandler::HandledSignal, this, &CClient::OnHandledSignal );

    QObject::connect ( QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &CClient::OnAboutToQuit );

    // start timer so that elapsed time works
    PreciseTime.start();

    // set gain delay timer to single-shot and connect handler function
    TimerGain.setSingleShot ( true );

    QObject::connect ( &TimerGain, &QTimer::timeout, this, &CClient::OnTimerRemoteChanGain );

    // start the socket (it is important to start the socket after all
    // initializations and connections)
    Socket.Start();

    // do an immediate start if a server address is given
    if ( !Settings.CommandlineOptions.connect.Value().isEmpty() )
    {
        Settings.StartConnection ( Settings.CommandlineOptions.connect.Value(), "" );
    }
}

CClient::~CClient()
{
    // if we were running, stop sound device
    if ( Sound.IsRunning() )
    {
        Sound.Stop();
    }

    // free audio encoders and decoders
    opus_custom_encoder_destroy ( OpusEncoderMono );
    opus_custom_decoder_destroy ( OpusDecoderMono );
    opus_custom_encoder_destroy ( OpusEncoderStereo );
    opus_custom_decoder_destroy ( OpusDecoderStereo );
    opus_custom_encoder_destroy ( Opus64EncoderMono );
    opus_custom_decoder_destroy ( Opus64DecoderMono );
    opus_custom_encoder_destroy ( Opus64EncoderStereo );
    opus_custom_decoder_destroy ( Opus64DecoderStereo );

    // free audio modes
    opus_custom_mode_destroy ( OpusMode );
    opus_custom_mode_destroy ( Opus64Mode );
}

void CClient::ApplySettings()
{
    Channel.SetDoAutoSockBufSize ( Settings.GetAutoSockBufSize() );
    Channel.SetSockBufNumFrames ( Settings.GetClientSockBufNumFrames() );

    AudioReverb.Clear();

    //### TODO: BEGIN ###//
    // After the sound-redesign implementation
    // Settings should store all Sound settings per device and
    // Set a CurrendSoundCard with settings of the selected sound card.
    // and we should call a new CSound function Sound.ApplySettings() here
    QString strError = Sound.SetDev ( Settings.GetAudioDevice() );
    Sound.SetLeftInputChannel ( Settings.GetInputChannel ( 0 ) );
    Sound.SetRightInputChannel ( Settings.GetInputChannel ( 1 ) );
    Sound.SetLeftOutputChannel ( Settings.GetOutputChannel ( 0 ) );
    Sound.SetRightOutputChannel ( Settings.GetOutputChannel ( 1 ) );
    // Sound.ApplySettings();
    //### TODO: END ###//

    Init();
    emit SoundDeviceChanged ( strError );
}

void CClient::OnAboutToQuit() {}

void CClient::OnSendProtMessage ( CVector<uint8_t> vecMessage )
{
    // the protocol queries me to call the function to send the message
    // send it through the network
    Socket.SendPacket ( vecMessage, Channel.GetAddress() );
}

void CClient::OnSendCLProtMessage ( CHostAddress InetAddr, CVector<uint8_t> vecMessage )
{
    // the protocol queries me to call the function to send the message
    // send it through the network
    Socket.SendPacket ( vecMessage, InetAddr );
}

void CClient::OnInvalidPacketReceived ( CHostAddress RecHostAddr )
{
    // message could not be parsed, check if the packet comes
    // from the server we just connected -> if yes, send
    // disconnect message since the server may not know that we
    // are not connected anymore
    if ( Channel.GetAddress() == RecHostAddr )
    {
        ConnLessProtocol.CreateCLDisconnection ( RecHostAddr );
    }
}

void CClient::OnDetectedCLMessage ( CVector<uint8_t> vecbyMesBodyData, int iRecID, CHostAddress RecHostAddr )
{
    // connection less messages are always processed
    ConnLessProtocol.ParseConnectionLessMessageBody ( vecbyMesBodyData, iRecID, RecHostAddr );
}

void CClient::OnServerJittBufSizeChanged ( int iNewJitBufSize )
{
    // we received a jitter buffer size changed message from the server,
    // only apply this value if auto jitter buffer size is enabled
    if ( Settings.GetAutoSockBufSize() )
    {
        // Note: Do not use the "SetServerSockBufNumFrames" function for setting
        // the new server jitter buffer size since then a message would be sent
        // to the server which is incorrect.
        Settings.SetServerSockBufNumFrames ( iNewJitBufSize );
    }
}

void CClient::CreateServerJitterBufferMessage()
{
    // per definition in the client: if auto jitter buffer is enabled, both,
    // the client and server shall use an auto jitter buffer

    if ( Settings.GetAutoSockBufSize() )
    {
        // in case auto jitter buffer size is enabled, we have to transmit a
        // special value
        Channel.CreateJitBufMes ( AUTO_NET_BUF_SIZE_FOR_PROTOCOL );
    }
    else
    {
        Channel.CreateJitBufMes ( Settings.GetServerSockBufNumFrames() );
    }
}

void CClient::OnCLPingReceived ( CHostAddress InetAddr, int iMs )
{
    // make sure we are running and the server address is correct
    if ( IsRunning() && ( InetAddr == Channel.GetAddress() ) )
    {
        // take care of wrap arounds (if wrapping, do not use result)
        const int iCurDiff = EvaluatePingMessage ( iMs );
        if ( iCurDiff >= 0 )
        {
            iCurPingTime = iCurDiff; // store for use by gain message sending

            emit PingTimeReceived ( iCurDiff );
        }
    }
}

void CClient::OnCLPingWithNumClientsReceived ( CHostAddress InetAddr, int iMs, int iNumClients )
{
    // take care of wrap arounds (if wrapping, do not use result)
    const int iCurDiff = EvaluatePingMessage ( iMs );
    if ( iCurDiff >= 0 )
    {
        emit CLPingTimeWithNumClientsReceived ( InetAddr, iCurDiff, iNumClients );
    }
}

int CClient::PreparePingMessage()
{
    // transmit the current precise time (in ms)
    return PreciseTime.elapsed();
}

int CClient::EvaluatePingMessage ( const int iMs )
{
    // calculate difference between received time in ms and current time in ms
    return PreciseTime.elapsed() - iMs;
}

// In order not to flood the server with gain change messages, particularly when using
// a MIDI controller, a timer is used to limit the rate at which such messages are generated.
// This avoids a potential long backlog of messages, since each must be ACKed before the next
// can be sent, and this ACK is subject to the latency of the server connection.
//
// When the first gain change message is requested after an idle period (i.e. the timer is not
// running), it will be sent immediately, and a 300ms timer started.
//
// If a gain change message is requested while the timer is still running, the new gain is not sent,
// but just stored in newGain[iId], and the minGainId and maxGainId updated to note the range of
// IDs that must be checked when the time expires (this will usually be a single channel
// unless channel grouping is being used). This avoids having to check all possible channels.
//
// When the timer fires, the channels minGainId <= iId < maxGainId are checked by comparing
// the last sent value in oldGain[iId] with any pending value in newGain[iId], and if they differ,
// the new value is sent, updating oldGain[iId] with the sent value. If any new values are
// sent, the timer is restarted so that further immediate updates will be pended.

void CClient::SetRemoteChanGain ( const int iId, const float fGain, const bool bIsMyOwnFader )
{
    QMutexLocker locker ( &MutexGain );

    // if this gain is for my own channel, apply the value for the Mute Myself function
    if ( bIsMyOwnFader )
    {
        fMuteOutStreamGain = fGain;
    }

    if ( TimerGain.isActive() )
    {
        // just update the new value for sending later;
        // will compare with oldGain[iId] when the timer fires
        newGain[iId] = fGain;

        // update range of channel IDs to check in the timer
        if ( iId < minGainId )
            minGainId = iId; // first value to check
        if ( iId >= maxGainId )
            maxGainId = iId + 1; // first value NOT to check

        return;
    }

    // here the timer was not active:
    // send the actual gain and reset the range of channel IDs to empty
    oldGain[iId] = newGain[iId] = fGain;
    Channel.SetRemoteChanGain ( iId, fGain );

    StartDelayTimer();
}

void CClient::OnTimerRemoteChanGain()
{
    QMutexLocker locker ( &MutexGain );
    bool         bSent = false;

    for ( int iId = minGainId; iId < maxGainId; iId++ )
    {
        if ( newGain[iId] != oldGain[iId] )
        {
            // send new gain and record as old gain
            float fGain = oldGain[iId] = newGain[iId];
            Channel.SetRemoteChanGain ( iId, fGain );
            bSent = true;
        }
    }

    // if a new gain has been sent, reset the range of channel IDs to empty and start timer
    if ( bSent )
    {
        StartDelayTimer();
    }
}

// reset the range of channel IDs to check and start the delay timer
void CClient::StartDelayTimer()
{
    maxGainId = 0;
    minGainId = MAX_NUM_CHANNELS;

    // start timer to delay sending further updates
    // use longer delay when connected to server with higher ping time,
    // double the ping time in order to allow a bit of overhead for other messages
    if ( iCurPingTime < DEFAULT_GAIN_DELAY_PERIOD_MS / 2 )
    {
        TimerGain.start ( DEFAULT_GAIN_DELAY_PERIOD_MS );
    }
    else
    {
        TimerGain.start ( iCurPingTime * 2 );
    }
}

bool CClient::SetServerAddr ( QString strNAddr )
{
    CHostAddress HostAddress;
    if ( NetworkUtil().ParseNetworkAddress ( strNAddr, HostAddress, bEnableIPv6 ) )
    {
        // apply address to the channel
        Channel.SetAddress ( HostAddress );

        return true;
    }
    else
    {
        return false; // invalid address
    }
}

bool CClient::GetAndResetbJitterBufferOKFlag()
{
    // get the socket buffer put status flag and reset it
    const bool bSocketJitBufOKFlag = Socket.GetAndResetbJitterBufferOKFlag();

    if ( !bJitterBufferOK )
    {
        // our jitter buffer get status is not OK so the overall status of the
        // jitter buffer is also not OK (we do not have to consider the status
        // of the socket buffer put status flag)

        // reset flag before returning the function
        bJitterBufferOK = true;
        return false;
    }

    // the jitter buffer get (our own status flag) is OK, the final status
    // now depends on the jitter buffer put status flag from the socket
    // since per definition the jitter buffer status is OK if both the
    // put and get status are OK
    return bSocketJitBufOKFlag;
}

void CClient::OnReInitRequest()
{
    // if client was running then first
    // stop it and restart again after new initialization
    const bool bWasRunning = Sound.IsRunning();
    if ( bWasRunning )
    {
        Sound.Stop();
    }

    Init();

    if ( bWasRunning )
    {
        Sound.Start();
    }
}

void CClient::OnReverbChannelChanged() { AudioReverb.Clear(); }

void CClient::OnChannelInfoChanged()
{
    // Update the channel
    Channel.SetRemoteInfo ( Settings.GetChannelInfo() );
}

void CClient::OnAudioDeviceChanged()
{
    const QString strNewDev = Settings.GetAudioDevice();

    // if client was running then first
    // stop it and restart again after new initialization
    const bool bWasRunning = Sound.IsRunning();
    if ( bWasRunning )
    {
        Sound.Stop();
    }

    //### TODO: BEGIN ###//
    // After the sound-redesign implementation
    // we should store all Sound related settings per Device and
    // we should read all current device settings for the new device
    // ( i.e. Settings.SelectAudioDevice ( strNewDev ) )
    // and call a new CSound function Sound.ApplySettings()
    // that should change device, channel selection and all other device related Sound settings all in one go

    const QString strError = Sound.SetDev ( strNewDev );
    Settings.SetAudioDevice ( Sound.GetDev() );

    // init again because the sound card actual buffer size might
    // be changed on new device
    Init();

    if ( bWasRunning )
    {
        // restart client
        Sound.Start();
    }

    emit SoundDeviceChanged ( strError );
    //### TODO: END ###//
}

void CClient::OnInputChannelChanged()
{
    // if client was running then first
    // stop it and restart again after new initialization
    const bool bWasRunning = Sound.IsRunning();
    if ( bWasRunning )
    {
        Sound.Stop();
    }

    Sound.SetLeftInputChannel ( Settings.GetInputChannel ( 0 ) );
    Sound.SetRightInputChannel ( Settings.GetInputChannel ( 1 ) );

    if ( Sound.GetLeftInputChannel() != Settings.GetInputChannel ( 0 ) )
    {
        Settings.SetInputChannel ( 0, Sound.GetLeftInputChannel() );
    }

    if ( Sound.GetRightInputChannel() != Settings.GetInputChannel ( 1 ) )
    {
        Settings.SetInputChannel ( 1, Sound.GetRightInputChannel() );
    }

    Init();

    if ( bWasRunning )
    {
        // restart client
        Sound.Start();
    }
}

void CClient::OnOutputChannelChanged()
{
    // if client was running then first
    // stop it and restart again after new initialization
    const bool bWasRunning = Sound.IsRunning();
    if ( bWasRunning )
    {
        Sound.Stop();
    }

    Sound.SetLeftOutputChannel ( Settings.GetOutputChannel ( 0 ) );
    Sound.SetRightOutputChannel ( Settings.GetOutputChannel ( 1 ) );

    if ( Sound.GetLeftOutputChannel() != Settings.GetOutputChannel ( 0 ) )
    {
        Settings.SetOutputChannel ( 0, Sound.GetLeftOutputChannel() );
    }

    if ( Sound.GetRightOutputChannel() != Settings.GetOutputChannel ( 1 ) )
    {
        Settings.SetOutputChannel ( 1, Sound.GetRightOutputChannel() );
    }

    Init();

    if ( bWasRunning )
    {
        // restart client
        Sound.Start();
    }
}

void CClient::OnPrefFrameSizeFactorChanged()
{
    const int iNewFactor = Settings.GetSndCrdPrefFrameSizeFactor();

    // first check new input parameter
    if ( ( iNewFactor == FRAME_SIZE_FACTOR_PREFERRED ) || ( iNewFactor == FRAME_SIZE_FACTOR_DEFAULT ) || ( iNewFactor == FRAME_SIZE_FACTOR_SAFE ) )
    {
        // init with new parameter, if client was running then first
        // stop it and restart again after new initialization
        const bool bWasRunning = Sound.IsRunning();
        if ( bWasRunning )
        {
            Sound.Stop();
        }

        // set new parameter
        iSndCrdPrefFrameSizeFactor = iNewFactor;

        // init with new block size index parameter
        Init();

        if ( bWasRunning )
        {
            // restart client
            Sound.Start();
        }
    }
    else
    {
        Settings.SetSndCrdPrefFrameSizeFactor ( iSndCrdPrefFrameSizeFactor );
    }
}

void CClient::OnSndCrdReinitRequest ( int iSndCrdResetType )
{
    QString strError = "";

    // audio device notifications can come at any time and they are in a
    // different thread, therefore we need a mutex here
    MutexDriverReinit.lock();
    {
        // in older QT versions, enums cannot easily be used in signals without
        // registering them -> workaroud: we use the int type and cast to the enum
        const ESndCrdResetType eSndCrdResetType = static_cast<ESndCrdResetType> ( iSndCrdResetType );

        // if client was running then first
        // stop it and restart again after new initialization
        const bool bWasRunning = Sound.IsRunning();
        if ( bWasRunning )
        {
            Sound.Stop();
        }

        // perform reinit request as indicated by the request type parameter
        if ( eSndCrdResetType != RS_ONLY_RESTART )
        {
            if ( eSndCrdResetType != RS_ONLY_RESTART_AND_INIT )
            {
                // reinit the driver if requested
                // (we use the currently selected driver)
                strError = Sound.SetDev ( Sound.GetDev() );
            }

            // init client object (must always be performed if the driver
            // was changed)
            Init();
        }

        if ( bWasRunning )
        {
            // restart client
            Sound.Start();
        }
    }
    MutexDriverReinit.unlock();

    // inform GUI about the sound card device change
    emit SoundDeviceChanged ( strError );
}

void CClient::OnHandledSignal ( int sigNum )
{
#ifdef _WIN32
    // Windows does not actually get OnHandledSignal triggered
    QCoreApplication::instance()->exit();
    Q_UNUSED ( sigNum )
#else
    switch ( sigNum )
    {
    case SIGINT:
    case SIGTERM:
        // if connected, terminate connection (needed for headless mode)
        if ( IsRunning() )
        {
            Stop();
        }

        // this should trigger OnAboutToQuit
        QCoreApplication::instance()->exit();
        break;

    default:
        break;
    }
#endif
}

void CClient::OnControllerInFaderLevel ( int iChannelIdx, int iValue )
{
    // in case of a headless client the faders cannot be moved so we need
    // to send the controller information directly to the server
#ifdef HEADLESS
    // only apply new fader level if channel index is valid
    if ( ( iChannelIdx >= 0 ) && ( iChannelIdx < MAX_NUM_CHANNELS ) )
    {
        SetRemoteChanGain ( iChannelIdx, MathUtils::CalcFaderGain ( iValue ), false );
    }
#endif

    emit ControllerInFaderLevel ( iChannelIdx, iValue );
}

void CClient::OnControllerInPanValue ( int iChannelIdx, int iValue )
{
    // in case of a headless client the panners cannot be moved so we need
    // to send the controller information directly to the server
#ifdef HEADLESS
    // channel index is valid
    SetRemoteChanPan ( iChannelIdx, static_cast<float> ( iValue ) / AUD_MIX_PAN_MAX );
#endif

    emit ControllerInPanValue ( iChannelIdx, iValue );
}

void CClient::OnControllerInFaderIsSolo ( int iChannelIdx, bool bIsSolo )
{
    // in case of a headless client the buttons are not displayed so we need
    // to send the controller information directly to the server
#ifdef HEADLESS
    // FIXME: no idea what to do here.
#endif

    emit ControllerInFaderIsSolo ( iChannelIdx, bIsSolo );
}

void CClient::OnControllerInFaderIsMute ( int iChannelIdx, bool bIsMute )
{
    // in case of a headless client the buttons are not displayed so we need
    // to send the controller information directly to the server
#ifdef HEADLESS
    // FIXME: no idea what to do here.
#endif

    emit ControllerInFaderIsMute ( iChannelIdx, bIsMute );
}

void CClient::OnControllerInMuteMyself ( bool bMute )
{
    // in case of a headless client the buttons are not displayed so we need
    // to send the controller information directly to the server
#ifdef HEADLESS
    // FIXME: no idea what to do here.
#endif

    emit ControllerInMuteMyself ( bMute );
}

void CClient::OnClientIDReceived ( int iChanID )
{
    // for headless mode we support to mute our own signal in the personal mix
    // (note that the check for headless is done in the main.cpp and must not
    // be checked here)
    if ( bMuteMeInPersonalMix )
    {
        SetRemoteChanGain ( iChanID, 0, false );
    }

    emit ClientIDReceived ( iChanID );
}

void CClient::OnConnectRequest()
{
    if ( !Channel.IsEnabled() )
    {
        // init object
        Init();

        // enable channel
        if ( SetServerAddr ( Settings.GetServerAddress() ) )
        {
            Settings.AckConnecting ( true );
            Channel.SetEnable ( true );

            // start audio interface
            Sound.Start();

            return;
        }
    }

    Settings.AckConnecting ( false );
}

void CClient::OnDisconnectRequest()
{
    if ( Channel.IsEnabled() )
    {
        Settings.AckDisconnecting ( true );

        // Send disconnect message to server
        ConnLessProtocol.CreateCLDisconnection ( Channel.GetAddress() );

        // initiate a disconnect in channel
        Channel.Disconnect();

        // wait some time for disconnection
        QTime DieTime = QTime::currentTime().addMSecs ( 250 );
        while ( QTime::currentTime() < DieTime && Settings.GetConnected() )
        {
            // exclude user input events because if we use AllEvents, it happens
            // that if the user initiates a connection and disconnection quickly
            // (e.g. quickly pressing enter five times), the software can get into
            // an unknown state
            QCoreApplication::processEvents ( QEventLoop::ExcludeUserInputEvents, 50 );
        }

        // Check if we timed out !
        if ( Settings.GetConnected() )
        {
            // Send disconnect message to server again
            ConnLessProtocol.CreateCLDisconnection ( Channel.GetAddress() );
            // and force disconnected state
            Settings.SetConnected ( false );
        }

        Sound.Stop();
        Channel.SetEnable ( false );
    }
    else
    {
        Settings.AckDisconnecting ( false );
    }

    // reset current signal level and LEDs
    bJitterBufferOK = true;
    SignalLevelMeter.Reset();
}

void CClient::OnConnected()
{
    // a new connection was successfully initiated, send infos and request
    // connected clients list
    Channel.SetRemoteInfo ( Settings.GetChannelInfo() );

    // We have to send a connected clients list request since it can happen
    // that we just had connected to the server and then disconnected but
    // the server still thinks that we are connected (the server is still
    // waiting for the channel time-out). If we now connect again, we would
    // not get the list because the server does not know about a new connection.
    // Same problem is with the jitter buffer message.
    Channel.CreateReqConnClientsList();
    CreateServerJitterBufferMessage();

    // clang-format off
// TODO needed for compatibility to old servers >= 3.4.6 and <= 3.5.12
Channel.CreateReqChannelLevelListMes();
    // clang-format on

    Settings.SetConnected ( true );
}

void CClient::OnDisconnected()
{
    Sound.Stop();
    Channel.SetEnable ( false );
    Settings.EndConnection();
    Settings.SetConnected ( false );
}

void CClient::Init()
{
    // check if possible frame size factors are supported
    const int iFraSizePreffered = SYSTEM_FRAME_SIZE_SAMPLES * FRAME_SIZE_FACTOR_PREFERRED;
    const int iFraSizeDefault   = SYSTEM_FRAME_SIZE_SAMPLES * FRAME_SIZE_FACTOR_DEFAULT;
    const int iFraSizeSafe      = SYSTEM_FRAME_SIZE_SAMPLES * FRAME_SIZE_FACTOR_SAFE;

#if defined( Q_OS_IOS )
    bFraSiFactPrefSupported = true; // to reduce sound init time, because we know it's supported in iOS
    bFraSiFactDefSupported  = true;
    bFraSiFactSafeSupported = true;
#else
    Settings.bFraSiFactPrefSupported = ( Sound.Init ( iFraSizePreffered ) == iFraSizePreffered );
    Settings.bFraSiFactDefSupported  = ( Sound.Init ( iFraSizeDefault ) == iFraSizeDefault );
    Settings.bFraSiFactSafeSupported = ( Sound.Init ( iFraSizeSafe ) == iFraSizeSafe );
#endif

    // translate block size index in actual block size
    const int iPrefMonoFrameSize = iSndCrdPrefFrameSizeFactor * SYSTEM_FRAME_SIZE_SAMPLES;

    // get actual sound card buffer size using preferred size
    // TODO - iOS needs 1 init only, now: 9 inits at launch <- slow
    // Initially, I tried to fix this as follows (inside #ifdef ios tag):
    //    if ( Sound.isInitialized )
    //      iMonoBlockSizeSam = iPrefMonoFrameSize;
    //    else
    //      iMonoBlockSizeSam = Sound.Init ( iPrefMonoFrameSize );
    // Problem is legitimate setting changes (buffer size for example).
    // so the condition should be something like "if ( Sound.isInitialized and APP_IS_INIALIZING)"
    iMonoBlockSizeSam = Sound.Init ( iPrefMonoFrameSize );

    // Calculate the current sound card frame size factor. In case
    // the current mono block size is not a multiple of the system
    // frame size, we have to use a sound card conversion buffer.
    if ( ( ( iMonoBlockSizeSam == ( SYSTEM_FRAME_SIZE_SAMPLES * FRAME_SIZE_FACTOR_PREFERRED ) ) && Settings.GetEnableOPUS64() ) ||
         ( iMonoBlockSizeSam == ( SYSTEM_FRAME_SIZE_SAMPLES * FRAME_SIZE_FACTOR_DEFAULT ) ) ||
         ( iMonoBlockSizeSam == ( SYSTEM_FRAME_SIZE_SAMPLES * FRAME_SIZE_FACTOR_SAFE ) ) )
    {
        // regular case: one of our predefined buffer sizes is available
        iSndCrdFrameSizeFactor = iMonoBlockSizeSam / SYSTEM_FRAME_SIZE_SAMPLES;

        // no sound card conversion buffer required
        bSndCrdConversionBufferRequired = false;
    }
    else
    {
        // An unsupported sound card buffer size is currently used -> we have
        // to use a conversion buffer. Per definition we use the smallest buffer
        // size as the current frame size.

        // store actual sound card buffer size (stereo)
        bSndCrdConversionBufferRequired  = true;
        iSndCardMonoBlockSizeSamConvBuff = iMonoBlockSizeSam;

        // overwrite block size factor by using one frame
        iSndCrdFrameSizeFactor = 1;
    }

    // select the OPUS frame size mode depending on current mono block size samples
    if ( bSndCrdConversionBufferRequired )
    {
        if ( ( iSndCardMonoBlockSizeSamConvBuff < DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES ) && Settings.GetEnableOPUS64() )
        {
            iMonoBlockSizeSam     = SYSTEM_FRAME_SIZE_SAMPLES;
            eAudioCompressionType = CT_OPUS64;
        }
        else
        {
            iMonoBlockSizeSam     = DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES;
            eAudioCompressionType = CT_OPUS;
        }
    }
    else
    {
        if ( iMonoBlockSizeSam < DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES )
        {
            eAudioCompressionType = CT_OPUS64;
        }
        else
        {
            // since we use double size frame size for OPUS, we have to adjust the frame size factor
            iSndCrdFrameSizeFactor /= 2;
            eAudioCompressionType = CT_OPUS;
        }
    }

    // inits for audio coding
    if ( eAudioCompressionType == CT_OPUS )
    {
        iOPUSFrameSizeSamples = DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES;

        if ( Settings.GetAudioChannelConfig() == CC_MONO )
        {
            CurOpusEncoder    = OpusEncoderMono;
            CurOpusDecoder    = OpusDecoderMono;
            iNumAudioChannels = 1;

            switch ( Settings.GetAudioQuality() )
            {
            case AQ_LOW:
                iCeltNumCodedBytes = OPUS_NUM_BYTES_MONO_LOW_QUALITY_DBLE_FRAMESIZE;
                break;
            case AQ_NORMAL:
                iCeltNumCodedBytes = OPUS_NUM_BYTES_MONO_NORMAL_QUALITY_DBLE_FRAMESIZE;
                break;
            case AQ_HIGH:
                iCeltNumCodedBytes = OPUS_NUM_BYTES_MONO_HIGH_QUALITY_DBLE_FRAMESIZE;
                break;
            }
        }
        else
        {
            CurOpusEncoder    = OpusEncoderStereo;
            CurOpusDecoder    = OpusDecoderStereo;
            iNumAudioChannels = 2;

            switch ( Settings.GetAudioQuality() )
            {
            case AQ_LOW:
                iCeltNumCodedBytes = OPUS_NUM_BYTES_STEREO_LOW_QUALITY_DBLE_FRAMESIZE;
                break;
            case AQ_NORMAL:
                iCeltNumCodedBytes = OPUS_NUM_BYTES_STEREO_NORMAL_QUALITY_DBLE_FRAMESIZE;
                break;
            case AQ_HIGH:
                iCeltNumCodedBytes = OPUS_NUM_BYTES_STEREO_HIGH_QUALITY_DBLE_FRAMESIZE;
                break;
            }
        }
    }
    else /* CT_OPUS64 */
    {
        iOPUSFrameSizeSamples = SYSTEM_FRAME_SIZE_SAMPLES;

        if ( Settings.GetAudioChannelConfig() == CC_MONO )
        {
            CurOpusEncoder    = Opus64EncoderMono;
            CurOpusDecoder    = Opus64DecoderMono;
            iNumAudioChannels = 1;

            switch ( Settings.GetAudioQuality() )
            {
            case AQ_LOW:
                iCeltNumCodedBytes = OPUS_NUM_BYTES_MONO_LOW_QUALITY;
                break;
            case AQ_NORMAL:
                iCeltNumCodedBytes = OPUS_NUM_BYTES_MONO_NORMAL_QUALITY;
                break;
            case AQ_HIGH:
                iCeltNumCodedBytes = OPUS_NUM_BYTES_MONO_HIGH_QUALITY;
                break;
            }
        }
        else
        {
            CurOpusEncoder    = Opus64EncoderStereo;
            CurOpusDecoder    = Opus64DecoderStereo;
            iNumAudioChannels = 2;

            switch ( Settings.GetAudioQuality() )
            {
            case AQ_LOW:
                iCeltNumCodedBytes = OPUS_NUM_BYTES_STEREO_LOW_QUALITY;
                break;
            case AQ_NORMAL:
                iCeltNumCodedBytes = OPUS_NUM_BYTES_STEREO_NORMAL_QUALITY;
                break;
            case AQ_HIGH:
                iCeltNumCodedBytes = OPUS_NUM_BYTES_STEREO_HIGH_QUALITY;
                break;
            }
        }
    }

    // calculate stereo (two channels) buffer size
    iStereoBlockSizeSam = 2 * iMonoBlockSizeSam;

    vecCeltData.Init ( iCeltNumCodedBytes );
    vecZeros.Init ( iStereoBlockSizeSam, 0 );
    vecsStereoSndCrdMuteStream.Init ( iStereoBlockSizeSam );

    opus_custom_encoder_ctl ( CurOpusEncoder,
                              OPUS_SET_BITRATE ( CalcBitRateBitsPerSecFromCodedBytes ( iCeltNumCodedBytes, iOPUSFrameSizeSamples ) ) );

    // inits for network and channel
    vecbyNetwData.Init ( iCeltNumCodedBytes );

    // set the channel network properties
    Channel.SetAudioStreamProperties ( eAudioCompressionType, iCeltNumCodedBytes, iSndCrdFrameSizeFactor, iNumAudioChannels );

    // init reverberation
    AudioReverb.Init ( Settings.GetAudioChannelConfig(), iStereoBlockSizeSam, SYSTEM_SAMPLE_RATE_HZ );

    // init the sound card conversion buffers
    if ( bSndCrdConversionBufferRequired )
    {
        // inits for conversion buffer (the size of the conversion buffer must
        // be the sum of input/output sizes which is the worst case fill level)
        const int iSndCardStereoBlockSizeSamConvBuff = 2 * iSndCardMonoBlockSizeSamConvBuff;
        const int iConBufSize                        = iStereoBlockSizeSam + iSndCardStereoBlockSizeSamConvBuff;

        SndCrdConversionBufferIn.Init ( iConBufSize );
        SndCrdConversionBufferOut.Init ( iConBufSize );
        vecDataConvBuf.Init ( iStereoBlockSizeSam );

        // the output conversion buffer must be filled with the inner
        // block size for initialization (this is the latency which is
        // introduced by the conversion buffer) to avoid buffer underruns
        SndCrdConversionBufferOut.Put ( vecZeros, iStereoBlockSizeSam );
    }

    // reset initialization phase flag and mute flag
    bIsInitializationPhase = true;
}

void CClient::AudioCallback ( CVector<int16_t>& psData, void* arg )
{
    // get the pointer to the object
    CClient* pMyClientObj = static_cast<CClient*> ( arg );

    // process audio data
    pMyClientObj->ProcessSndCrdAudioData ( psData );

    // clang-format off
/*
// TEST do a soundcard jitter measurement
static CTimingMeas JitterMeas ( 1000, "test2.dat" );
JitterMeas.Measure();
*/
    // clang-format on
}

void CClient::ProcessSndCrdAudioData ( CVector<int16_t>& vecsStereoSndCrd )
{
    // check if a conversion buffer is required or not
    if ( bSndCrdConversionBufferRequired )
    {
        // add new sound card block in conversion buffer
        SndCrdConversionBufferIn.Put ( vecsStereoSndCrd, vecsStereoSndCrd.Size() );

        // process all available blocks of data
        while ( SndCrdConversionBufferIn.GetAvailData() >= iStereoBlockSizeSam )
        {
            // get one block of data for processing
            SndCrdConversionBufferIn.Get ( vecDataConvBuf, iStereoBlockSizeSam );

            // process audio data
            ProcessAudioDataIntern ( vecDataConvBuf );

            SndCrdConversionBufferOut.Put ( vecDataConvBuf, iStereoBlockSizeSam );
        }

        // get processed sound card block out of the conversion buffer
        SndCrdConversionBufferOut.Get ( vecsStereoSndCrd, vecsStereoSndCrd.Size() );
    }
    else
    {
        // regular case: no conversion buffer required
        // process audio data
        ProcessAudioDataIntern ( vecsStereoSndCrd );
    }
}

void CClient::ProcessAudioDataIntern ( CVector<int16_t>& vecsStereoSndCrd )
{
    int            i, j, iUnused;
    unsigned char* pCurCodedData;

    // Transmit signal ---------------------------------------------------------

    if ( Settings.GetInputBoost() != 1 )
    {
        // apply a general gain boost to all audio input:
        for ( i = 0, j = 0; i < iMonoBlockSizeSam; i++, j += 2 )
        {
            vecsStereoSndCrd[j + 1] = static_cast<int16_t> ( Settings.GetInputBoost() * vecsStereoSndCrd[j + 1] );
            vecsStereoSndCrd[j]     = static_cast<int16_t> ( Settings.GetInputBoost() * vecsStereoSndCrd[j] );
        }
    }

    // update stereo signal level meter (not needed in headless mode)
#ifndef HEADLESS
    SignalLevelMeter.Update ( vecsStereoSndCrd, iMonoBlockSizeSam, true );
#endif

    // add reverberation effect if activated
    if ( Settings.GetReverbLevel() != 0 )
    {
        AudioReverb.Process ( vecsStereoSndCrd,
                              Settings.GetReverbOnLeftChannel(),
                              static_cast<float> ( Settings.GetReverbLevel() ) / AUD_REVERB_MAX / 4 );
    }

    // apply balance and mix mono signals
    if ( !( ( Settings.GetAudioInputBalance() == AUD_FADER_IN_MIDDLE ) && ( Settings.GetAudioChannelConfig() == CC_STEREO ) ) )
    {
        // calculate pan gain in the range 0 to 1, where 0.5 is the middle position
        const float fPan = static_cast<float> ( Settings.GetAudioInputBalance() ) / AUD_FADER_IN_MAX;

        if ( Settings.GetAudioChannelConfig() == CC_STEREO )
        {
            // for stereo only apply pan attenuation on one channel (same as pan in the server)
            const float fGainL = MathUtils::GetLeftPan ( fPan, false );
            const float fGainR = MathUtils::GetRightPan ( fPan, false );

            for ( i = 0, j = 0; i < iMonoBlockSizeSam; i++, j += 2 )
            {
                // note that the gain is always <= 1, therefore a simple cast is
                // ok since we never can get an overload
                vecsStereoSndCrd[j + 1] = static_cast<int16_t> ( fGainR * vecsStereoSndCrd[j + 1] );
                vecsStereoSndCrd[j]     = static_cast<int16_t> ( fGainL * vecsStereoSndCrd[j] );
            }
        }
        else
        {
            // for mono implement a cross-fade between channels and mix them, for
            // mono-in/stereo-out use no attenuation in pan center
            const float fGainL = MathUtils::GetLeftPan ( fPan, Settings.GetAudioChannelConfig() != CC_MONO_IN_STEREO_OUT );
            const float fGainR = MathUtils::GetRightPan ( fPan, Settings.GetAudioChannelConfig() != CC_MONO_IN_STEREO_OUT );

            for ( i = 0, j = 0; i < iMonoBlockSizeSam; i++, j += 2 )
            {
                // note that we need the Float2Short for stereo pan mode
                vecsStereoSndCrd[i] = Float2Short ( fGainL * vecsStereoSndCrd[j] + fGainR * vecsStereoSndCrd[j + 1] );
            }
        }
    }

    // Support for mono-in/stereo-out mode: Per definition this mode works in
    // full stereo mode at the transmission level. The only thing which is done
    // is to mix both sound card inputs together and then put this signal on
    // both stereo channels to be transmitted to the server.
    if ( Settings.GetAudioChannelConfig() == CC_MONO_IN_STEREO_OUT )
    {
        // copy mono data in stereo sound card buffer (note that since the input
        // and output is the same buffer, we have to start from the end not to
        // overwrite input values)
        for ( i = iMonoBlockSizeSam - 1, j = iStereoBlockSizeSam - 2; i >= 0; i--, j -= 2 )
        {
            vecsStereoSndCrd[j] = vecsStereoSndCrd[j + 1] = vecsStereoSndCrd[i];
        }
    }

    for ( i = 0; i < iSndCrdFrameSizeFactor; i++ )
    {
        // OPUS encoding
        if ( CurOpusEncoder != nullptr )
        {
            if ( Settings.bMuteOutStream )
            {
                iUnused = opus_custom_encode ( CurOpusEncoder,
                                               &vecZeros[i * iNumAudioChannels * iOPUSFrameSizeSamples],
                                               iOPUSFrameSizeSamples,
                                               &vecCeltData[0],
                                               iCeltNumCodedBytes );
            }
            else
            {
                iUnused = opus_custom_encode ( CurOpusEncoder,
                                               &vecsStereoSndCrd[i * iNumAudioChannels * iOPUSFrameSizeSamples],
                                               iOPUSFrameSizeSamples,
                                               &vecCeltData[0],
                                               iCeltNumCodedBytes );
            }
        }

        // send coded audio through the network
        Channel.PrepAndSendPacket ( &Socket, vecCeltData, iCeltNumCodedBytes );
    }

    // Receive signal ----------------------------------------------------------
    // in case of mute stream, store local data
    if ( Settings.bMuteOutStream )
    {
        vecsStereoSndCrdMuteStream = vecsStereoSndCrd;
    }

    for ( i = 0; i < iSndCrdFrameSizeFactor; i++ )
    {
        // receive a new block
        const bool bReceiveDataOk = ( Channel.GetData ( vecbyNetwData, iCeltNumCodedBytes ) == GS_BUFFER_OK );

        // get pointer to coded data and manage the flags
        if ( bReceiveDataOk )
        {
            pCurCodedData = &vecbyNetwData[0];

            // on any valid received packet, we clear the initialization phase flag
            bIsInitializationPhase = false;
        }
        else
        {
            // for lost packets use null pointer as coded input data
            pCurCodedData = nullptr;

            // invalidate the buffer OK status flag
            bJitterBufferOK = false;
        }

        // OPUS decoding
        if ( CurOpusDecoder != nullptr )
        {
            iUnused = opus_custom_decode ( CurOpusDecoder,
                                           pCurCodedData,
                                           iCeltNumCodedBytes,
                                           &vecsStereoSndCrd[i * iNumAudioChannels * iOPUSFrameSizeSamples],
                                           iOPUSFrameSizeSamples );
        }
    }

    // for muted stream we have to add our local data here
    if ( Settings.bMuteOutStream )
    {
        for ( i = 0; i < iStereoBlockSizeSam; i++ )
        {
            vecsStereoSndCrd[i] = Float2Short ( vecsStereoSndCrd[i] + vecsStereoSndCrdMuteStream[i] * fMuteOutStreamGain );
        }
    }

    // check if channel is connected and if we do not have the initialization phase
    if ( Channel.IsConnected() && ( !bIsInitializationPhase ) )
    {
        if ( Settings.GetAudioChannelConfig() == CC_MONO )
        {
            // copy mono data in stereo sound card buffer (note that since the input
            // and output is the same buffer, we have to start from the end not to
            // overwrite input values)
            for ( i = iMonoBlockSizeSam - 1, j = iStereoBlockSizeSam - 2; i >= 0; i--, j -= 2 )
            {
                vecsStereoSndCrd[j] = vecsStereoSndCrd[j + 1] = vecsStereoSndCrd[i];
            }
        }
    }
    else
    {
        // if not connected, clear data
        vecsStereoSndCrd.Reset ( 0 );
    }

    // update socket buffer size
    Channel.UpdateSocketBufferSize();

    Q_UNUSED ( iUnused )
}

int CClient::EstimatedOverallDelay ( const int iPingTimeMs )
{
    const float fSystemBlockDurationMs = static_cast<float> ( iOPUSFrameSizeSamples ) / SYSTEM_SAMPLE_RATE_HZ * 1000;

    // If the jitter buffers are set effectively, i.e. they are exactly the
    // size of the network jitter, then the delay of the buffer is the buffer
    // length. Since that is usually not the case but the buffers are usually
    // a bit larger than necessary, we introduce some factor for compensation.
    // Consider the jitter buffer on the client and on the server side, too.

    const float fTotalJitterBufferDelayMs =
        fSystemBlockDurationMs * ( Settings.GetClientSockBufNumFrames() + Settings.GetServerSockBufNumFrames() ) * 0.7f;

    // consider delay introduced by the sound card conversion buffer by using
    // "GetSndCrdConvBufAdditionalDelayMonoBlSize()"
    float fTotalSoundCardDelayMs = GetSndCrdConvBufAdditionalDelayMonoBlSize() * 1000.0f / SYSTEM_SAMPLE_RATE_HZ;

    // try to get the actual input/output sound card delay from the audio
    // interface, per definition it is not available if a 0 is returned
    const float fSoundCardInputOutputLatencyMs = Sound.GetInOutLatencyMs();

    if ( fSoundCardInputOutputLatencyMs == 0.0f )
    {
        // use an alternative approach for estimating the sound card delay:
        //
        // we assume that we have two period sizes for the input and one for the
        // output, therefore we have "3 *" instead of "2 *" (for input and output)
        // the actual sound card buffer size
        // "GetSndCrdConvBufAdditionalDelayMonoBlSize"
        fTotalSoundCardDelayMs += ( 3 * GetSndCrdActualMonoBlSize() ) * 1000.0f / SYSTEM_SAMPLE_RATE_HZ;
    }
    else
    {
        // add the actual sound card latency in ms
        fTotalSoundCardDelayMs += fSoundCardInputOutputLatencyMs;
    }

    // network packets are of the same size as the audio packets per definition
    // if no sound card conversion buffer is used
    const float fDelayToFillNetworkPacketsMs = GetSystemMonoBlSize() * 1000.0f / SYSTEM_SAMPLE_RATE_HZ;

    // OPUS additional delay at small frame sizes is half a frame size
    const float fAdditionalAudioCodecDelayMs = fSystemBlockDurationMs / 2;

    const float fTotalBufferDelayMs =
        fDelayToFillNetworkPacketsMs + fTotalJitterBufferDelayMs + fTotalSoundCardDelayMs + fAdditionalAudioCodecDelayMs;

    return MathUtils::round ( fTotalBufferDelayMs + iPingTimeMs );
}
