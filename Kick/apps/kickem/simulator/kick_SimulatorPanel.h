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

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "input_events/input_events.h"
#include "input_events/kick_SimulatorInterface.h"
#include "contracts/dcl/dcl_contracts.h"

#include <functional>
#include <vector>

namespace sim
{
    /// A panel button that reports both press and release, like the hardware.
    class PanelButton : public juce::TextButton
    {
    public:
        PanelButton( const juce::String& label, int code );

        void setLit( bool lit );

        const int code;

        std::function<void( int code, bool pressed )> onPress;

    private:
        void mouseDown( const juce::MouseEvent& e ) override;
        void mouseUp( const juce::MouseEvent& e ) override;
    };

    /// One of the 16 pads. Velocity follows where it is pressed: the top edge is loudest.
    class PadComponent : public juce::Component
    {
    public:
        explicit PadComponent( int pad );

        /// Transparent means the pad's light is off.
        void setLight( juce::Colour colour );

        std::function<void( int pad, bool pressed, int velocity )> onPress;

    private:
        void paint( juce::Graphics& g ) override;
        void mouseDown( const juce::MouseEvent& e ) override;
        void mouseUp( const juce::MouseEvent& e ) override;

        const int    pad;
        bool         down  = false;
        juce::Colour light = juce::Colours::transparentBlack;
    };

    /// An endless encoder: drag up/down or use the mouse wheel. The centre knob also clicks.
    class KnobComponent : public juce::Component
    {
    public:
        KnobComponent( const juce::String& label, int knob );

        std::function<void( int knob, int steps )> onTurn;
        std::function<void( bool pressed )>        onClick;

    private:
        void paint( juce::Graphics& g ) override;
        void mouseDown( const juce::MouseEvent& e ) override;
        void mouseDrag( const juce::MouseEvent& e ) override;
        void mouseUp( const juce::MouseEvent& e ) override;
        void mouseWheelMove( const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel ) override;
        void turn( int steps );

        const juce::String label;
        const int          knob;
        float              angle      = 0.0f;
        int                dragY      = 0;
        bool               moved      = false;
        float              wheelSteps = 0.0f;
    };

    /// On-screen stand-in for the KICK's front panel and its MCU. Sends the same
    /// CPktMgr::HWMsg_t messages the MCU sends, and shows the LED and pad-colour
    /// messages the app sends back.
    class SimulatorPanel : public juce::Component
        , public SimulatorInterface
    {
    public:
        SimulatorPanel();
        ~SimulatorPanel() override;

        /// Window area left free for the KICK's screen.
        void setScreenArea( juce::Rectangle<int> area );

        static constexpr int margin      = 16;
        static constexpr int columnWidth = 400;
        static constexpr int stripHeight = 4 * 32 + 3 * 6;

        void sendMessageToHW( const CPktMgr::HWMsg_t& f_HwMsg ) override;

    private:
        void paint( juce::Graphics& g ) override;
        void resized() override;

        void send( CPktMgr::enumPacketType type, int cmd, std::vector<int> data );
        void apply( const CPktMgr::HWMsg_t& msg );
        void setPad( int pad, int state, int colourId );
        void setLed( int code, bool lit );

        juce::Rectangle<int> screenArea;
        juce::Rectangle<int> sliderLedArea;
        int                  sliderLed = 0;

        juce::OwnedArray<PadComponent>  pads;
        juce::OwnedArray<KnobComponent> knobs;
        juce::OwnedArray<PanelButton>   bankButtons, transportButtons, modeButtons;
        juce::Slider                    slider;

        JUCE_DECLARE_WEAK_REFERENCEABLE( SimulatorPanel )
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR( SimulatorPanel )
    };
}
