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

// KICK desktop simulator: the Kick app (same engine, device adapter and UI as apps/kick)
// with an on-screen front panel standing in for the panel MCU, in one window.

#include "kick_ui/kick_ui.h"
#include "audio_engine/audio_engine.h"
#include "../kick/kick_device_adapter.h"
#include "../kick/kick_audio_engine_adapter.h"
#include "../kick/kick_audio_interface.h"
#include "simulator/kick_SimulatorPanel.h"

//==============================================================================
class SimulatorContent : public juce::Component
{
public:
    explicit SimulatorContent( sim::SimulatorPanel& p )
        : panel( p )
    {
        addAndMakeVisible( panel );
        addAndMakeVisible( kick_ui::KickHome::Instance() );

        const int m = sim::SimulatorPanel::margin;
        setSize( m + ui::layout::getAppWidth() + m + sim::SimulatorPanel::columnWidth + m,
                 m + ui::layout::getAppHeight() + 12 + sim::SimulatorPanel::stripHeight + m );
    }

    ~SimulatorContent() override
    {
        // KickHome is a singleton; this window shows it but does not own it.
        removeChildComponent( &kick_ui::KickHome::Instance() );
        removeChildComponent( &panel );
    }

    void resized() override
    {
        const int  m      = sim::SimulatorPanel::margin;
        const auto screen = juce::Rectangle<int>( m, m, ui::layout::getAppWidth(), ui::layout::getAppHeight() );
        panel.setBounds( getLocalBounds() );
        panel.setScreenArea( screen );
        kick_ui::KickHome::Instance().setBounds( screen );
    }

private:
    sim::SimulatorPanel& panel;
};

//==============================================================================
class KickSimulatorApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "KICK Simulator"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool               moreThanOneInstanceAllowed() override { return false; }

    void initialise( const juce::String& /*commandLine*/ ) override
    {
        // The panel must be attached before the device adapter initialises input events.
        panel = std::make_unique<sim::SimulatorPanel>();
        Controls::InputEvents::attachSimulator( panel.get() );

        audioInterface.reset( new KickAudioInterface() );
        engineAdapter.reset( new KickAudioEngineAdapter( audioInterface ) );
        adapter.reset( new KickDeviceAdapter() );

        mt::getSettings();
        mt::getAudioInterface();

        content = std::make_unique<SimulatorContent>( *panel );
        window  = std::make_unique<MainWindow>( getApplicationName(), *content );

        if( auto path = juce::SystemStats::getEnvironmentVariable( "KICK_SNAPSHOT", {} ); path.isNotEmpty() )
        {
            // Desktop debugging aid: KICK_SNAPSHOT=<file.png> saves the window after
            // KICK_SNAPSHOT_DELAY_MS (default 4000) and quits.
            const int delay = juce::SystemStats::getEnvironmentVariable( "KICK_SNAPSHOT_DELAY_MS", "4000" ).getIntValue();
            juce::Timer::callAfterDelay( delay, [this, path] {
                auto       image = content->createComponentSnapshot( content->getLocalBounds() );
                juce::File file( path );
                file.deleteFile();
                juce::FileOutputStream out( file );
                juce::PNGImageFormat().writeImageToStream( image, out );
                out.flush();
                quit();
            } );
        }
    }

    void shutdown() override
    {
        window  = nullptr;
        content = nullptr;
        adapter = nullptr;
        Controls::InputEvents::attachSimulator( nullptr );
        panel = nullptr;
    }

    void systemRequestedQuit() override
    {
        auto mgr = juce::ModalComponentManager::getInstance();
        if( mgr->cancelAllModalComponents() )
            juce::Timer::callAfterDelay( 100, [this] { quit(); } );
        else
            quit();
    }

    void anotherInstanceStarted( const juce::String& /*commandLine*/ ) override {}

    //==============================================================================
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow( const juce::String& name, juce::Component& c )
            : juce::DocumentWindow( name, juce::Colours::black, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton )
        {
            setUsingNativeTitleBar( true );
            setContentNonOwned( &c, true );
            setResizable( false, false );
            centreWithSize( getWidth(), getHeight() );
            setVisible( true );
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

        void inputAttemptWhenModal() override
        {
            auto numModal = getNumCurrentlyModalComponents();
            auto index    = juce::jmax( 0, numModal - 1 );
            if( auto p = getCurrentlyModalComponent( index ) )
                p->toFront( true );
        }
    };

private:
    std::unique_ptr<sim::SimulatorPanel>       panel;
    std::shared_ptr<contracts::AudioInterface> audioInterface;
    std::unique_ptr<KickAudioEngineAdapter>    engineAdapter;
    std::unique_ptr<KickDeviceAdapter>         adapter;
    std::unique_ptr<SimulatorContent>          content;
    std::unique_ptr<MainWindow>                window;
};

START_JUCE_APPLICATION( KickSimulatorApplication )
