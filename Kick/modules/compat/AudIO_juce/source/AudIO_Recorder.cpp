/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

namespace AudIO
{
    namespace
    {
        // Calls a callback on the message thread. The callback is copied, so it
        // stays valid even if the recorder is destroyed first.
        template<typename Fn, typename... Args>
        void notify( const Fn& Callback, Args... args )
        {
            if( !Callback )
                return;
            if( juce::MessageManager::existsAndIsCurrentThread() )
                Callback( args... );
            else
                juce::MessageManager::callAsync( [Callback, args...]() { Callback( args... ); } );
        }
    } // namespace

    //==============================================================================
    Recorder::Recorder( Design& D, const std::string& Name )
        : Object( 2, 2, 0 )
        , DawRouting( D, BusType::Object, BusWidth::Stereo )
    {
        Object::setName( Name );
        DawRouting::setID( getId() );
    }

    Recorder::~Recorder() { cancelPendingUpdate(); }

    void Recorder::prepareToPlay( uint32_t SampleRate, uint32_t BlockSize )
    {
        Object::prepareToPlay( SampleRate, BlockSize );
        DawInserts::prepareToPlay( SampleRate, BlockSize );

        // Grow only; the audio thread uses these as per-block scratch.
        if( scratchL.size() < BlockSize )
        {
            scratchL.resize( BlockSize );
            scratchR.resize( BlockSize );
        }
    }

    void Recorder::setLength( double TimeInSeconds )
    {
        TimeInSeconds        = juce::jmax( 0.0, TimeInSeconds );
        const auto numFrames = (int)std::ceil( TimeInSeconds * getSampleRate() );
        const auto numCh     = numChannelsFor( mode.load() );

        const juce::SpinLock::ScopedLockType lock( bufferLock );
        buffer.setSize( numCh, numFrames, true, true, false );
        lengthInSeconds = TimeInSeconds;
    }

    MeterData Recorder::readInputMeter() { return metering.getMeterIn( inMeter ); }
    MeterData Recorder::readOutputMeter() { return metering.getMeter( outMeter ); }

    bool Recorder::saveToFile( const juce::File& File )
    {
        juce::AudioBuffer<float> copy;
        {
            const juce::SpinLock::ScopedLockType lock( bufferLock );
            copy.makeCopyOf( buffer );
        }
        if( !fileHasAudio( copy ) )
            return false;

        File.getParentDirectory().createDirectory();
        File.deleteFile();

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream( File.createOutputStream() );
        if( !stream )
            return false;

        std::unique_ptr<juce::AudioFormatWriter> writer( wav.createWriterFor( stream.get(), getSampleRate(), (unsigned int)copy.getNumChannels(), 24, {}, 0 ) );
        if( !writer )
            return false;

        stream.release(); // Owned by the writer now.
        return writer->writeFromAudioSampleBuffer( copy, 0, copy.getNumSamples() );
    }

    void Recorder::readInputs( Buffer<float>& Buf, int NumSamples, float* Left, float* Right, float GainL, float GainR ) const noexcept
    {
        const float* inL = Buf.getInputBuffer( 0 );
        const float* inR = Buf.getInputBuffer( 1 );

        for( int i = 0; i < NumSamples; ++i )
        {
            const float l = inL ? inL[i] : 0.f;
            const float r = inR ? inR[i] : 0.f;

            switch( mode.load() )
            {
                case L_MONO: Left[i] = Right[i] = l * GainL; break;
                case R_MONO: Left[i] = Right[i] = r * GainR; break;
                case LR_MONO: Left[i] = Right[i] = 0.5f * ( l * GainL + r * GainR ); break;
                case L_STEREO: Left[i] = Right[i] = l * GainL; break;
                case R_STEREO: Left[i] = Right[i] = r * GainR; break;
                default:
                case LR_STEREO:
                    Left[i]  = l * GainL;
                    Right[i] = r * GainR;
                    break;
            }
        }
    }

    bool Recorder::inputCrossesThreshold( Buffer<float>& Buf, int NumSamples ) const noexcept
    {
        const auto t = threshold.load();
        if( t <= 0.f )
            return true;

        for( uint32_t ch = 0; ch < 2; ++ch )
            if( const float* in = Buf.getInputBuffer( ch ) )
                for( int i = 0; i < NumSamples; ++i )
                    if( std::abs( in[i] ) >= t )
                        return true;
        return false;
    }

    void Recorder::writeMonitor( Buffer<float>& Buf, int NumSamples ) const noexcept
    {
        float* outL = Buf.getOutputBuffer( 0 );
        float* outR = Buf.getOutputBuffer( 1 );
        if( !monitor.load() )
        {
            if( outL ) juce::FloatVectorOperations::clear( outL, NumSamples );
            if( outR ) juce::FloatVectorOperations::clear( outR, NumSamples );
            return;
        }

        // scratchL/R hold the mode-routed input at this point.
        const auto g = monitorGain.load();
        if( outL ) juce::FloatVectorOperations::copyWithMultiply( outL, scratchL.data(), g, NumSamples );
        if( outR ) juce::FloatVectorOperations::copyWithMultiply( outR, scratchR.data(), g, NumSamples );
    }

    void Recorder::panGains( float& GainL, float& GainR ) const noexcept
    {
        // Balance law: centre is unity on both sides.
        const auto g = gain.load();
        const auto p = pan.load();
        GainL        = g * juce::jmin( 1.f, 1.f - p );
        GainR        = g * juce::jmin( 1.f, 1.f + p );
    }

    //==============================================================================
    SampleRecorder::SampleRecorder( Design& D )
        : Recorder( D, "SampleRecorder" )
    {
    }

    void SampleRecorder::reset()
    {
        recording.store( false );
        previewing.store( false );
        thresholdCrossed.store( false );

        const juce::SpinLock::ScopedLockType lock( bufferLock );
        buffer.setSize( numChannelsFor( mode.load() ), 0 );
        lengthInSeconds = 0;
        writePos.store( 0 );
        readPos.store( 0 );
        recordedFrom.store( 0 );
        recordedTo.store( 0 );
    }

    void SampleRecorder::resetWritePosition() noexcept
    {
        const auto n = buffer.getNumSamples();
        writePos.store( isForward() ? 0 : juce::jmax( 0, n - 1 ) );
        recordedFrom.store( isForward() ? 0 : n );
        recordedTo.store( isForward() ? 0 : n );
    }

    void SampleRecorder::startRecording( bool FromBeginning )
    {
        previewing.store( false );
        {
            // The channel count follows the mode, so re-allocate at the current length.
            const juce::SpinLock::ScopedLockType lock( bufferLock );
            const auto numFrames = (int)std::ceil( lengthInSeconds * getSampleRate() );
            const auto numCh     = numChannelsFor( mode.load() );
            if( FromBeginning || buffer.getNumChannels() != numCh || buffer.getNumSamples() != numFrames )
            {
                buffer.setSize( numCh, numFrames, false, true, false );
                resetWritePosition();
            }
        }
        thresholdCrossed.store( false );
        finishedRecording.store( false );
        recording.store( 0 < buffer.getNumSamples() );

        if( recording.load() )
            notify( onRecorderStarted );
    }

    void SampleRecorder::stopRecording()
    {
        // The audio thread clears `recording` itself when the buffer fills, and
        // leaves `finishedRecording` set so the callbacks still fire here.
        const bool wasRecording = recording.exchange( false ) | finishedRecording.exchange( false );

        {
            // Trim the buffer to what was actually captured.
            const juce::SpinLock::ScopedLockType lock( bufferLock );
            const int from = juce::jlimit( 0, buffer.getNumSamples(), recordedFrom.load() );
            const int to   = juce::jlimit( from, buffer.getNumSamples(), recordedTo.load() );
            if( from > 0 ) // source and destination overlap: memmove, not copyFrom
                for( int ch = 0; ch < buffer.getNumChannels(); ++ch )
                    std::memmove( buffer.getWritePointer( ch ), buffer.getReadPointer( ch, from ), sizeof( float ) * (size_t)( to - from ) );
            buffer.setSize( buffer.getNumChannels(), to - from, true, true, false );
            recordedFrom.store( 0 );
            recordedTo.store( to - from );
            lengthInSeconds = ( to - from ) / getSampleRate();
        }

        if( wasRecording )
        {
            notify( onRecorderLengthChanged, lengthInSeconds );
            notify( onRecorderStopped );
        }
    }

    void SampleRecorder::startPreview( double FromTimeInSeconds )
    {
        if( recording.load() || !fileHasAudio( buffer ) )
            return;

        readPos.store( juce::jlimit( 0, buffer.getNumSamples() - 1, (int)( FromTimeInSeconds * getSampleRate() ) ) );
        finishedPreview.store( false );
        previewing.store( true );
        notify( onRecorderPreviewStarted );
    }

    void SampleRecorder::stopPreview()
    {
        if( previewing.exchange( false ) )
            notify( onRecorderPreviewStopped );
    }

    void SampleRecorder::saveRecording( const std::string& FilePath )
    {
        if( recording.load() )
            stopRecording();
        stopPreview();
        saveToFile( juce::File( FilePath ) );
    }

    float SampleRecorder::readProgress() const noexcept
    {
        const auto n = buffer.getNumSamples();
        if( n <= 0 )
            return 0.f;
        const auto written = isForward() ? writePos.load() : ( n - 1 - writePos.load() );
        return juce::jlimit( 0.f, 1.f, (float)written / (float)n );
    }

    void SampleRecorder::onAudioThreadEvent()
    {
        if( finishedRecording.load() )
            stopRecording();
        if( finishedPreview.exchange( false ) )
            stopPreview();
    }

    void SampleRecorder::process( Buffer<float>& Buf )
    {
        const int numSamples = (int)juce::jmin( (size_t)blockSize(), scratchL.size() );
        metering.processIn( Buf );

        if( !isEnabled() )
        {
            Buf.clearOutputs();
            metering.process( Buf );
            return;
        }

        float gL, gR;
        panGains( gL, gR );
        readInputs( Buf, numSamples, scratchL.data(), scratchR.data(), gL, gR );
        writeMonitor( Buf, numSamples );

        const juce::SpinLock::ScopedTryLockType lock( bufferLock );
        if( !lock.isLocked() )
        {
            metering.process( Buf );
            return;
        }

        const int len = buffer.getNumSamples();

        if( recording.load() && 0 < len )
        {
            if( !thresholdCrossed.load() && inputCrossesThreshold( Buf, numSamples ) )
                thresholdCrossed.store( true );

            if( thresholdCrossed.load() )
            {
                const bool  dub  = isOverdubbing();
                const bool  fwd  = isForward();
                const int   step = fwd ? 1 : -1;
                const int   nCh  = buffer.getNumChannels();
                float*      d0   = buffer.getWritePointer( 0 );
                float*      d1   = ( 1 < nCh ) ? buffer.getWritePointer( 1 ) : nullptr;
                int         w    = writePos.load();

                for( int i = 0; i < numSamples; ++i )
                {
                    if( w < 0 || w >= len )
                    {
                        if( !isLooping() )
                        {
                            recording.store( false );
                            finishedRecording.store( true );
                            postFromAudioThread();
                            break;
                        }
                        w = fwd ? 0 : len - 1;
                    }

                    // Mono modes store the routed input in a single channel.
                    d0[w] = dub ? 0.5f * ( d0[w] + scratchL[i] ) : scratchL[i];
                    if( d1 )
                        d1[w] = dub ? 0.5f * ( d1[w] + scratchR[i] ) : scratchR[i];

                    if( fwd )
                        recordedTo.store( juce::jmax( recordedTo.load(), w + 1 ) );
                    else
                        recordedFrom.store( juce::jmin( recordedFrom.load(), w ) );
                    w += step;
                }
                writePos.store( w );
            }
        }
        else if( previewing.load() && 0 < len )
        {
            float*       outL = Buf.getOutputBuffer( 0 );
            float*       outR = Buf.getOutputBuffer( 1 );
            const float* s0   = buffer.getReadPointer( 0 );
            const float* s1   = buffer.getReadPointer( juce::jmin( 1, buffer.getNumChannels() - 1 ) );
            int          r    = readPos.load();

            for( int i = 0; i < numSamples; ++i, ++r )
            {
                if( r >= len )
                {
                    previewing.store( false );
                    finishedPreview.store( true );
                    postFromAudioThread();
                    r = 0;
                    break;
                }
                if( outL ) outL[i] += s0[r];
                if( outR ) outR[i] += s1[r];
            }
            readPos.store( r );
        }

        metering.process( Buf );
    }

    //==============================================================================
    Looper::Looper( Design& D )
        : Recorder( D, "Looper" )
    {
        looping.store( true );
    }

    void Looper::setRecord( bool Record ) noexcept
    {
        record.store( Record );
        if( Record )
            overdub.store( false );
    }

    void Looper::setOverdub( bool Overdub ) noexcept
    {
        overdub.store( Overdub );
        if( Overdub )
            record.store( false );
    }

    void Looper::setPlay() noexcept
    {
        record.store( false );
        overdub.store( false );
        if( !isEmpty() )
            running.store( true );
    }

    void Looper::takeUndoSnapshot()
    {
        undoBuffer.makeCopyOf( buffer, true ); // pre-sized in setLength: no allocation here
        undoEmpty = isEmpty();                 // so the first take can be undone back to empty
        hasUndo   = true;
        undone    = false;
    }

    void Looper::setLength( double TimeInSeconds )
    {
        TimeInSeconds        = juce::jmax( 0.0, TimeInSeconds );
        const auto numFrames = (int)std::ceil( TimeInSeconds * getSampleRate() );

        // Allocate outside the lock; swap in under it so the audio thread never waits on an allocation.
        juce::AudioBuffer<float> resized( 2, numFrames );
        resized.clear();
        undoBuffer.setSize( 2, numFrames, false, false, true );
        {
            const juce::SpinLock::ScopedLockType lock( bufferLock );
            for( int ch = 0; ch < juce::jmin( 2, buffer.getNumChannels() ); ++ch )
                resized.copyFrom( ch, 0, buffer, ch, 0, juce::jmin( numFrames, buffer.getNumSamples() ) );
            std::swap( buffer, resized );
            lengthInSeconds = TimeInSeconds;
            pos.store( juce::jmin( pos.load(), juce::jmax( 0, numFrames - 1 ) ) );
        }
        hasUndo = undone = false;
    }

    void Looper::start( bool FromBeginning, bool SkipThresholdDetection )
    {
        // Ensure a stereo loop of the current length (Looper::setLength allocates it).
        const auto numFrames = (int)std::ceil( lengthInSeconds * getSampleRate() );
        if( buffer.getNumChannels() != 2 || buffer.getNumSamples() != numFrames )
            setLength( lengthInSeconds );

        {
            const juce::SpinLock::ScopedLockType lock( bufferLock );
            takeUndoSnapshot();
            if( FromBeginning )
                pos.store( 0 );
        }
        thresholdCrossed.store( SkipThresholdDetection );
        running.store( 0 < buffer.getNumSamples() );
    }

    void Looper::play( double FromTimeInSeconds )
    {
        record.store( false );
        overdub.store( false );
        if( isEmpty() )
            return;
        pos.store( juce::jlimit( 0, juce::jmax( 0, buffer.getNumSamples() - 1 ), (int)( FromTimeInSeconds * getSampleRate() ) ) );
        running.store( true );
    }

    void Looper::stop()
    {
        running.store( false );
        record.store( false );
        overdub.store( false );
    }

    void Looper::clear()
    {
        stop();
        const juce::SpinLock::ScopedLockType lock( bufferLock );
        buffer.clear();
        undoBuffer.setSize( 0, 0 );
        hasUndo = undone = false;
        empty.store( true );
        pos.store( 0 );
        progress = 0;
    }

    void Looper::undo()
    {
        if( !hasUndo || undone )
            return;
        const juce::SpinLock::ScopedLockType lock( bufferLock );
        std::swap( buffer, undoBuffer );
        swapEmpty();
        undone = true;
    }

    void Looper::redo()
    {
        if( !hasUndo || !undone )
            return;
        const juce::SpinLock::ScopedLockType lock( bufferLock );
        std::swap( buffer, undoBuffer );
        swapEmpty();
        undone = false;
    }

    void Looper::swapEmpty()
    {
        const bool was = empty.load();
        empty.store( undoEmpty );
        undoEmpty = was;
    }

    void Looper::save( const juce::String& FilePath ) { saveToFile( juce::File( FilePath ) ); }

    double& Looper::getProgress() noexcept
    {
        const auto n = buffer.getNumSamples();
        progress     = ( n > 0 ) ? (double)pos.load() / (double)n : 0.0;
        return progress;
    }

    void Looper::process( Buffer<float>& Buf )
    {
        const int numSamples = (int)juce::jmin( (size_t)blockSize(), scratchL.size() );
        metering.processIn( Buf );

        if( !isEnabled() )
        {
            Buf.clearOutputs();
            metering.process( Buf );
            return;
        }

        // Input is recorded unity-gain; gain and pan are for playback.
        readInputs( Buf, numSamples, scratchL.data(), scratchR.data(), 1.f, 1.f );
        writeMonitor( Buf, numSamples );

        const juce::SpinLock::ScopedTryLockType lock( bufferLock );
        const int len = ( lock.isLocked() && buffer.getNumChannels() >= 2 ) ? buffer.getNumSamples() : 0;

        if( running.load() && 0 < len )
        {
            const bool writing = record.load() || overdub.load();
            if( writing && !thresholdCrossed.load() && inputCrossesThreshold( Buf, numSamples ) )
                thresholdCrossed.store( true );

            float gL, gR;
            panGains( gL, gR );

            float*     d0   = buffer.getWritePointer( 0 );
            float*     d1   = buffer.getWritePointer( 1 );
            float*     outL = Buf.getOutputBuffer( 0 );
            float*     outR = Buf.getOutputBuffer( 1 );
            const bool fwd  = isForward();
            int        p    = pos.load();

            for( int i = 0; i < numSamples; ++i )
            {
                p = ( p % len + len ) % len;

                // Play what was there before this pass writes over it.
                if( outL ) outL[i] += d0[p] * gL;
                if( outR ) outR[i] += d1[p] * gR;

                if( writing && thresholdCrossed.load() )
                {
                    if( overdub.load() )
                    {
                        d0[p] += scratchL[i];
                        d1[p] += scratchR[i];
                    }
                    else
                    {
                        d0[p] = scratchL[i];
                        d1[p] = scratchR[i];
                    }
                    empty.store( false );
                }
                p += fwd ? 1 : -1;
            }
            pos.store( ( p % len + len ) % len );
        }

        applyInserts( Buf, numSamples );
        metering.process( Buf );
    }

    void Looper::prepareToPlay( uint32_t SampleRate, uint32_t BlockSize )
    {
        Recorder::prepareToPlay( SampleRate, BlockSize );
        if( !fxBuffer )
            fxBuffer = std::make_unique<Buffer<float>>( 2, true, 2, true );
    }

    void Looper::applyInserts( Buffer<float>& Buf, int NumSamples )
    {
        // Insert effects read inputs and write outputs, so run them on a scratch buffer
        // fed with the looper's output (the real inputs belong to upstream objects).
        bool anyActive = false;
        for( unsigned int i = 0; i < (unsigned int)DawInserts::MAX_INSERTS; ++i )
            anyActive = anyActive || !isEffectBypassed( i );
        if( !anyActive || isAllEffectsBypassed() || !fxBuffer )
            return;

        for( uint32_t ch = 0; ch < 2; ++ch )
        {
            auto* src = Buf.getOutputBuffer( ch );
            auto* dst = fxBuffer->getInputBuffer( ch );
            if( src && dst )
                juce::FloatVectorOperations::copy( dst, src, NumSamples );
        }
        DawInserts::process( *fxBuffer );
        for( uint32_t ch = 0; ch < 2; ++ch )
        {
            auto* src = fxBuffer->getOutputBuffer( ch );
            auto* dst = Buf.getOutputBuffer( ch );
            if( src && dst )
                juce::FloatVectorOperations::copy( dst, src, NumSamples );
        }
    }
} // namespace AudIO
