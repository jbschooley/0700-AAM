/*
 * This file is part of Kick.
 *
 * Kick is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Kick is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with Kick. If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "kick_SimulatorPanel.h"

namespace sim
{
    using DCC = contracts::DeviceControlContracts;

    namespace
    {
        constexpr int numPads = 16;

        // DeviceControlContracts::PadColourIds. The real colours live in the panel MCU's
        // firmware, which is not released; these are approximations from the names.
        juce::Colour padColour( int colourId )
        {
            static const juce::uint32 colours[] = { 0xff8b0000, 0xff9f00ff, 0xffb8a000, 0xff90ee90, 0xff66cc66, 0xff3cb371,
                                                    0xff87cefa, 0xff00e5ff, 0xff1e5bff, 0xffff9ec4, 0xffff6fae, 0xffff3d8b,
                                                    0xffff2020, 0xffffa040, 0xffff7f00, 0x00000000, 0xfff0f0f0, 0xff888888 };
            if( colourId < 0 || colourId >= (int)std::size( colours ) )
                return juce::Colours::transparentBlack;
            return juce::Colour( colours[colourId] );
        }

        const juce::Colour panelBackground{ 0xff1d1d1d };
        const juce::Colour sectionBackground{ 0xff262626 };
        const juce::Colour ledColour{ 0xfff2a900 };

        struct ButtonDef
        {
            const char* label;
            int         code;
        };

        const ButtonDef bankDefs[] = { { "A", DCC::EN_BANK_A }, { "B", DCC::EN_BANK_B }, { "C", DCC::EN_BANK_C }, { "D", DCC::EN_BANK_D },
                                       { "E", DCC::EN_BANK_E }, { "F", DCC::EN_BANK_F }, { "G", DCC::EN_BANK_G }, { "H", DCC::EN_BANK_H } };

        const ButtonDef transportDefs[] = { { "Record", DCC::EN_RECORD }, { "Overdub", DCC::EN_OVERDUB }, { "Stop", DCC::EN_STOP },
                                            { "Play", DCC::EN_PLAY }, { "Play Start", DCC::EN_PLAYSTART } };

        const ButtonDef modeDefs[] = { { "Main", DCC::EN_MAINMODE }, { "Grid", DCC::EN_GRID }, { "Browse", DCC::EN_BROWSE },
                                       { "Save", DCC::EN_SAVE }, { "Track Mix", DCC::EN_TRACK_MIX }, { "Pad Mix", DCC::EN_PAD_MIX },
                                       { "Track Mute", DCC::EN_TRACK_MUTE }, { "Pad Mute", DCC::EN_PAD_MUTE }, { "Next Seq", DCC::EN_NEXT_SEQ },
                                       { "FX", DCC::EN_FX }, { "Tap Tempo", DCC::EN_TAP_TEMPO }, { "Master", DCC::EN_MASTER },
                                       { "Copy", DCC::EN_COPY }, { "+", DCC::EN_PLUS }, { "-", DCC::EN_MINUS }, { "Menu", DCC::EN_MENU },
                                       { "Pref", DCC::EN_PREF }, { "Note Repeat", DCC::EN_NOTE_REPEAT }, { "Latch", DCC::EN_LATCH },
                                       { "Back", DCC::EN_BACK }, { "Shift", DCC::EN_SHIFT }, { "Step Seq", DCC::EN_STEP_SEQ },
                                       { "Automation", DCC::EN_AUTOMATION }, { "TC", DCC::EN_TC }, { "On/Off", DCC::EN_ON_OFF },
                                       { "Sampler", DCC::EN_SAMPLER }, { "Looper", DCC::EN_LOOPER }, { "Sample Edit", DCC::EN_SAMPLE_EDIT },
                                       { "Link Edit", DCC::EN_LINK_EDIT }, { "Prog Edit", DCC::EN_PROG_EDIT }, { "MIDI Control", DCC::EN_MIDI_CONTROL },
                                       { "Link", DCC::EN_LINK }, { "Full Level", DCC::EN_FULL_LEVEL }, { "Half Level", DCC::EN_HALF_LEVEL },
                                       { "16 Levels", DCC::EN_16LEVELS }, { "Notes", DCC::EN_NOTES }, { "Erase", DCC::EN_ERASE } };

        constexpr int modeColumns = 10;
    }

    //==============================================================================
    PanelButton::PanelButton( const juce::String& label, int c )
        : juce::TextButton( label )
        , code( c )
    {
        setLit( false );
    }

    void PanelButton::setLit( bool lit )
    {
        setColour( juce::TextButton::buttonColourId, lit ? ledColour.withAlpha( 0.35f ) : juce::Colour( 0xff2f2f2f ) );
        setColour( juce::TextButton::textColourOffId, lit ? ledColour : juce::Colour( 0xffd8d8d8 ) );
    }

    void PanelButton::mouseDown( const juce::MouseEvent& e )
    {
        juce::TextButton::mouseDown( e );
        if( onPress ) onPress( code, true );
    }

    void PanelButton::mouseUp( const juce::MouseEvent& e )
    {
        juce::TextButton::mouseUp( e );
        if( onPress ) onPress( code, false );
    }

    //==============================================================================
    PadComponent::PadComponent( int p )
        : pad( p )
    {
    }

    void PadComponent::setLight( juce::Colour colour )
    {
        light = colour;
        repaint();
    }

    void PadComponent::paint( juce::Graphics& g )
    {
        auto area = getLocalBounds().toFloat().reduced( 1.0f );
        g.setColour( light.isTransparent() ? juce::Colour( 0xff1b1b1b ) : light );
        g.fillRoundedRectangle( area, 6.0f );
        g.setColour( down ? juce::Colours::white : juce::Colour( 0xff3a3a3a ) );
        g.drawRoundedRectangle( area, 6.0f, down ? 3.0f : 1.0f );
        g.setColour( light.isTransparent() ? juce::Colour( 0xff8a8a8a ) : juce::Colours::black.withAlpha( 0.6f ) );
        g.setFont( 12.0f );
        g.drawText( juce::String( pad + 1 ), area.reduced( 6.0f, 4.0f ), juce::Justification::bottomLeft );
    }

    void PadComponent::mouseDown( const juce::MouseEvent& e )
    {
        const auto proportion = 1.0f - (float)e.y / (float)juce::jmax( 1, getHeight() );
        const int  velocity   = juce::jlimit( 1, 127, juce::roundToInt( 127.0f * proportion ) + 20 );
        down                  = true;
        repaint();
        if( onPress ) onPress( pad, true, velocity );
    }

    void PadComponent::mouseUp( const juce::MouseEvent& )
    {
        down = false;
        repaint();
        if( onPress ) onPress( pad, false, 0 );
    }

    //==============================================================================
    KnobComponent::KnobComponent( const juce::String& l, int k )
        : label( l )
        , knob( k )
    {
    }

    void KnobComponent::paint( juce::Graphics& g )
    {
        auto       area  = getLocalBounds();
        const auto text  = area.removeFromBottom( 16 );
        const auto size  = (float)juce::jmin( area.getWidth(), area.getHeight() ) - 4.0f;
        const auto dial  = area.toFloat().withSizeKeepingCentre( size, size );
        const auto pivot = dial.getCentre();

        g.setColour( juce::Colour( 0xff2b2b2b ) );
        g.fillEllipse( dial );
        g.setColour( juce::Colour( 0xff474747 ) );
        g.drawEllipse( dial, 2.0f );

        juce::Line<float> pointer( pivot, pivot.withY( dial.getY() + 5.0f ) );
        pointer.applyTransform( juce::AffineTransform::rotation( juce::degreesToRadians( angle ), pivot.x, pivot.y ) );
        g.setColour( ledColour );
        g.drawLine( pointer, 2.5f );

        g.setColour( juce::Colour( 0xffd8d8d8 ) );
        g.setFont( 12.0f );
        g.drawText( label, text, juce::Justification::centred );
    }

    void KnobComponent::turn( int steps )
    {
        if( steps == 0 )
            return;
        angle += 12.0f * (float)steps;
        repaint();
        if( onTurn ) onTurn( knob, steps );
    }

    void KnobComponent::mouseDown( const juce::MouseEvent& e )
    {
        dragY = e.y;
        moved = false;
    }

    void KnobComponent::mouseDrag( const juce::MouseEvent& e )
    {
        const int steps = ( dragY - e.y ) / 8;
        if( steps != 0 )
        {
            dragY -= steps * 8;
            moved = true;
            turn( steps );
        }
    }

    void KnobComponent::mouseUp( const juce::MouseEvent& )
    {
        if( !moved && onClick )
        {
            onClick( true );
            onClick( false );
        }
    }

    void KnobComponent::mouseWheelMove( const juce::MouseEvent&, const juce::MouseWheelDetails& wheel )
    {
        wheelSteps += wheel.deltaY * 8.0f;
        const int steps = (int)wheelSteps;
        wheelSteps -= (float)steps;
        turn( steps );
    }

    //==============================================================================
    SimulatorPanel::SimulatorPanel()
    {
        for( int i = 0; i < numPads; ++i )
        {
            auto* p    = pads.add( new PadComponent( i ) );
            p->onPress = [this]( int pad, bool pressed, int velocity ) {
                send( CPktMgr::EN_PKT_TYPE_PAD, DCC::EN_PAD_STATE, { pad, pressed ? DCC::BTN_PRESSED : DCC::BTN_RELEASED, velocity, 0 } );
            };
            addAndMakeVisible( p );
        }

        auto* centre    = knobs.add( new KnobComponent( "Centre", DCC::EN_KNOB_CENTER ) );
        centre->onClick = [this]( bool pressed ) {
            send( CPktMgr::EN_PKT_TYPE_BUTTON, DCC::EN_BTN_STATUS, { DCC::EN_KNOB_ENTER, pressed ? DCC::BTN_PRESSED : DCC::BTN_RELEASED } );
        };
        for( int k = DCC::EN_KNOB_1; k <= DCC::EN_KNOB_4; ++k )
            knobs.add( new KnobComponent( "Knob " + juce::String( k ), k ) );
        for( auto* k : knobs )
        {
            k->onTurn = [this]( int knob, int steps ) {
                const int cmd = knob == DCC::EN_KNOB_CENTER ? DCC::EN_KNOB_ROTARY : DCC::EN_KNOB_QLINK;
                send( CPktMgr::EN_PKT_TYPE_KNOBS, cmd, { knob, steps > 0 ? DCC::EN_KNOB_CLOCKWISE : DCC::EN_KNOB_ANTICLOCKWISE, std::abs( steps ), 0 } );
            };
            addAndMakeVisible( k );
        }

        slider.setSliderStyle( juce::Slider::LinearHorizontal );
        slider.setTextBoxStyle( juce::Slider::NoTextBox, true, 0, 0 );
        slider.setRange( 0.0, 100.0, 1.0 );
        slider.onValueChange = [this] {
            send( CPktMgr::EN_PKT_TYPE_SLIDER, DCC::EN_SLIDER_TOUCH_INPUT, { (int)slider.getValue() } );
        };
        addAndMakeVisible( slider );

        auto addButtons = [this]( juce::OwnedArray<PanelButton>& list, const ButtonDef* defs, size_t count ) {
            for( size_t i = 0; i < count; ++i )
            {
                auto* b    = list.add( new PanelButton( defs[i].label, defs[i].code ) );
                b->onPress = [this]( int code, bool pressed ) {
                    send( CPktMgr::EN_PKT_TYPE_BUTTON, DCC::EN_BTN_STATUS, { code, pressed ? DCC::BTN_PRESSED : DCC::BTN_RELEASED } );
                };
                addAndMakeVisible( b );
            }
        };
        addButtons( bankButtons, bankDefs, std::size( bankDefs ) );
        addButtons( transportButtons, transportDefs, std::size( transportDefs ) );
        addButtons( modeButtons, modeDefs, std::size( modeDefs ) );
    }

    SimulatorPanel::~SimulatorPanel()
    {
        masterReference.clear();
    }

    void SimulatorPanel::setScreenArea( juce::Rectangle<int> area )
    {
        screenArea = area;
        resized();
    }

    void SimulatorPanel::send( CPktMgr::enumPacketType type, int cmd, std::vector<int> data )
    {
        CPktMgr::HWMsg_t msg;
        msg.pktType = type;
        msg.pktDir  = CPktMgr::EN_PKT_REQUEST;
        msg.pktCmd  = cmd;
        msg.data    = std::move( data );
        hwInputReceived( msg );
    }

    void SimulatorPanel::sendMessageToHW( const CPktMgr::HWMsg_t& f_HwMsg )
    {
        // May be called from any thread; update the panel on the message thread.
        juce::WeakReference<SimulatorPanel> self( this );
        juce::MessageManager::callAsync( [self, msg = f_HwMsg] {
            if( auto* panel = self.get() )
                panel->apply( msg );
        } );
    }

    void SimulatorPanel::setPad( int pad, int state, int colourId )
    {
        if( pad < 0 || pad >= pads.size() )
            return;
        pads[pad]->setLight( state != DCC::StateOff ? padColour( colourId ) : juce::Colours::transparentBlack );
    }

    void SimulatorPanel::setLed( int code, bool lit )
    {
        for( auto* list : { &bankButtons, &transportButtons, &modeButtons } )
            for( auto* b : *list )
                if( b->code == code )
                    b->setLit( lit );
    }

    void SimulatorPanel::apply( const CPktMgr::HWMsg_t& msg )
    {
        const auto& d = msg.data;
        switch( msg.pktType )
        {
            case CPktMgr::EN_PKT_TYPE_LED:
                if( ( msg.pktCmd == DCC::EN_LED_SET_SINGLE_LED || msg.pktCmd == DCC::EN_LED_BLINK_SINGLE ) && !d.empty() )
                    setLed( d[0], d.size() < 2 || d[1] != DCC::StateOff );
                break;

            case CPktMgr::EN_PKT_TYPE_PAD:
                switch( msg.pktCmd )
                {
                    case DCC::EN_PAD_SET_SINGLE_PAD:
                    case DCC::EN_PAD_SET_STEP_SINGLE_PAD:
                    case DCC::EN_PAD_SET_BLINKING:
                        if( d.size() >= 3 ) setPad( d[0], d[1], d[2] );
                        break;
                    case DCC::EN_PAD_SET_ALL_SINGLE_COLOUR:
                        if( d.size() >= 2 )
                            for( int i = 0; i < numPads; ++i ) setPad( i, d[0], d[1] );
                        break;
                    case DCC::EN_PAD_SET_ALL_MULTI_COLOUR:
                    case DCC::EN_PAD_SET_STEP_ALL_PAD:
                        for( int i = 0; i < numPads && i + 1 < (int)d.size(); ++i ) setPad( i, d[0], d[(size_t)i + 1] );
                        break;
                    case DCC::EN_PAD_SET_COLOUR_STATE:
                    {
                        const int n = (int)d.size() / 2;
                        for( int i = 0; i < juce::jmin( n, numPads ); ++i ) setPad( i, d[(size_t)( n + i )], d[(size_t)i] );
                        break;
                    }
                    default:
                        break;
                }
                break;

            case CPktMgr::EN_PKT_TYPE_SLIDER:
                if( msg.pktCmd != DCC::EN_SLIDER_TOUCH_INPUT && !d.empty() )
                {
                    sliderLed = juce::jlimit( 0, 100, d[0] );
                    repaint( sliderLedArea );
                }
                break;

            default:
                break;
        }
    }

    void SimulatorPanel::paint( juce::Graphics& g )
    {
        g.fillAll( panelBackground );
        g.setColour( juce::Colours::black );
        g.fillRect( screenArea.expanded( 2 ) );

        g.setColour( sectionBackground );
        g.fillRoundedRectangle( sliderLedArea.toFloat(), 2.0f );
        g.setColour( ledColour );
        g.fillRoundedRectangle( sliderLedArea.withWidth( sliderLedArea.getWidth() * sliderLed / 100 ).toFloat(), 2.0f );
    }

    void SimulatorPanel::resized()
    {
        constexpr int gap = 8;

        // Right-hand column: knobs, touch slider, pads, banks, transport.
        auto column = juce::Rectangle<int>( screenArea.getRight() + margin, margin, columnWidth, getHeight() - 2 * margin );

        auto knobRow = column.removeFromTop( 100 );
        knobs[0]->setBounds( knobRow.removeFromLeft( 96 ) );
        knobRow.removeFromLeft( gap );
        const int smallKnob = ( knobRow.getWidth() - 3 * gap ) / 4;
        for( int i = 1; i < knobs.size(); ++i )
        {
            knobs[i]->setBounds( knobRow.removeFromLeft( smallKnob ).withTrimmedTop( 24 ) );
            knobRow.removeFromLeft( gap );
        }

        column.removeFromTop( gap );
        sliderLedArea = column.removeFromTop( 6 );
        slider.setBounds( column.removeFromTop( 26 ) );

        column.removeFromTop( gap );
        const int padSize = ( column.getWidth() - 3 * gap ) / 4;
        auto      padArea = column.removeFromTop( 4 * padSize + 3 * gap );
        for( int i = 0; i < pads.size(); ++i )
        {
            // Pad 1 is bottom-left, as on the hardware.
            const int row = 3 - i / 4;
            const int col = i % 4;
            pads[i]->setBounds( padArea.getX() + col * ( padSize + gap ), padArea.getY() + row * ( padSize + gap ), padSize, padSize );
        }

        auto layoutRow = []( juce::OwnedArray<PanelButton>& list, juce::Rectangle<int> row ) {
            const int w = ( row.getWidth() - ( list.size() - 1 ) * 4 ) / juce::jmax( 1, list.size() );
            for( auto* b : list )
            {
                b->setBounds( row.removeFromLeft( w ) );
                row.removeFromLeft( 4 );
            }
        };
        column.removeFromTop( gap );
        layoutRow( bankButtons, column.removeFromTop( 30 ) );
        column.removeFromTop( gap );
        layoutRow( transportButtons, column.removeFromTop( 34 ) );

        // Strip under the screen: mode and function buttons.
        auto      strip = juce::Rectangle<int>( screenArea.getX(), screenArea.getBottom() + 12, screenArea.getWidth(), stripHeight );
        const int w     = ( strip.getWidth() - ( modeColumns - 1 ) * 6 ) / modeColumns;
        for( int i = 0; i < modeButtons.size(); ++i )
            modeButtons[i]->setBounds( strip.getX() + ( i % modeColumns ) * ( w + 6 ), strip.getY() + ( i / modeColumns ) * 38, w, 32 );
    }
}
