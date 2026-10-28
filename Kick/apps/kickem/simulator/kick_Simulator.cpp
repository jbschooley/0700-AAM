#include <kick_ui/kick_ui.h>
#include <program_edit_ui/program_edit_ui.h>
#include <program_edit_ui/sources/instrument/kick_ModularEditor.h>
#include "kick_Simulator.h"
#include "kick_SimulatorInterfaceImpl.h"

namespace sim
{
    namespace
    {
        const juce::Rectangle<int> screenRect { 620, 20, 1024, 600 };

        /// Diagnostics (set KICKEM_TRACE_MOUSE=1): log every mouse-down the app receives.
        struct MouseTrace : juce::MouseListener
        {
            void mouseDown( const juce::MouseEvent& e ) override
            {
                SimulatorInterfaceImpl::log( "mouseDown on " + juce::String( typeid( *e.eventComponent ).name() ) + " at " + e.getScreenPosition().toString()
                                             + " modal=" + juce::String( juce::Component::getNumCurrentlyModalComponents() ) );
            }
        } mouseTrace;
    }

    //==============================================================================
    // SimulatorBoard
    //==============================================================================
    SimulatorBoard::SimulatorBoard()
        : screen( kick_ui::KickHome::Instance() )
    {
        addAndMakeVisible( screen );

        addAndMakeVisible( slider );
        addAndMakeVisible( masterKnob );
        addAndMakeVisible( recKnob );
        addAndMakeVisible( fullLevelBtn );
        addAndMakeVisible( levels16Btn );
        addAndMakeVisible( eraseBtn );
        addAndMakeVisible( pads );
        addAndMakeVisible( mainControl );

        for( int i = 0; i < 4; ++i )
        {
            qlinks[i] = std::make_unique<SimKnob>( "P" + juce::String( i + 1 ), SimKnob::Mode::endless );
            qlinks[i]->onStep = [i]( int steps ) {
                hw::send( hw::knobMessage( DCC::EN_KNOB_1 + i, steps ) );
            };
            addAndMakeVisible( *qlinks[i] );
        }
        for( auto* b : { &linkBtn, &recBtn, &overdubBtn, &stopBtn, &playBtn, &playStartBtn } )
            addAndMakeVisible( b );

        recBtn.setLedColour( juce::Colour( 0xffff2020 ) );
        overdubBtn.setLedColour( juce::Colour( 0xffff2020 ) );
        playBtn.setLedColour( juce::Colour( 0xff20e040 ) );
        playStartBtn.setLedColour( juce::Colour( 0xff20e040 ) );

        // LED routing table: both functions of a dual-function button share its LED.
        std::vector<SimButton*> buttons { &fullLevelBtn, &levels16Btn, &eraseBtn, &linkBtn, &recBtn, &overdubBtn, &stopBtn, &playBtn, &playStartBtn };
        mainControl.collectButtons( buttons );
        for( auto* b : buttons )
        {
            if( b->getCode() != DCC::EN_INVALID_ID ) ledMap.emplace( b->getCode(), b );
            if( b->getShiftCode() != DCC::EN_INVALID_ID ) ledMap.emplace( b->getShiftCode(), b );
        }

        ShiftLatch::onChange = [this]( bool latched ) {
            mainControl.getShiftButton().setLed( latched );
            repaint(); // shift legends brighten while latched
        };

        outputConnection = SimulatorInterfaceImpl::outputReceived.connect( [this]( const CPktMgr::HWMsg_t& m ) { handleOutput( m ); } );

        setSize( boardWidth, boardHeight ); // last: resized() needs every child

        if( juce::SystemStats::getEnvironmentVariable( "KICKEM_TRACE_MOUSE", {} ).isNotEmpty() )
            juce::Desktop::getInstance().addGlobalMouseListener( &mouseTrace );

        if( juce::SystemStats::getEnvironmentVariable( "KICKEM_SELFTEST", {} ).isNotEmpty() )
            startSelfTest();
    }

    SimButton* SimulatorBoard::findButton( int code )
    {
        auto range = ledMap.equal_range( code );
        for( auto it = range.first; it != range.second; ++it )
            if( it->second->getCode() == code )
                return it->second;
        return nullptr;
    }

    void SimulatorBoard::startSelfTest()
    {
        auto& s     = selfTestSteps;
        auto  note  = [&s]( juce::String text ) { s.push_back( [text] { SimulatorInterfaceImpl::log( "SELFTEST " + text ); } ); };
        auto  wait  = [&s]( int n ) { for( int i = 0; i < n; ++i ) s.push_back( [] {} ); };
        auto  click = [&s, this]( int code ) {
            s.push_back( [this, code] { if( auto* b = findButton( code ) ) b->press( true ); } );
            s.push_back( [this, code] { if( auto* b = findButton( code ) ) b->press( false ); } );
        };
        auto pad = [&s, this]( int i ) {
            s.push_back( [this, i] { pads.getPad( i ).hit( true, 100 ); } );
            s.push_back( [this, i] { pads.getPad( i ).hit( false ); } );
        };

        if( juce::SystemStats::getEnvironmentVariable( "KICKEM_SELFTEST", {} ) == "instrument" )
        {
            startInstrumentTest();
            return;
        }
        if( juce::SystemStats::getEnvironmentVariable( "KICKEM_SELFTEST", {} ) == "audio" )
        {
            startAudioSettingsTest();
            return;
        }
        if( juce::SystemStats::getEnvironmentVariable( "KICKEM_SELFTEST", {} ) == "tour" )
        {
            startTourTest();
            return;
        }
        if( juce::SystemStats::getEnvironmentVariable( "KICKEM_SELFTEST", {} ) == "popup" )
        {
            startPopupTest();
            return;
        }
        if( juce::SystemStats::getEnvironmentVariable( "KICKEM_SELFTEST", {} ) == "picker" )
        {
            startPickerTest();
            return;
        }
        if( juce::SystemStats::getEnvironmentVariable( "KICKEM_SELFTEST", {} ) == "resources" )
        {
            startResourcesTest();
            return;
        }
        if( juce::SystemStats::getEnvironmentVariable( "KICKEM_SELFTEST", {} ) == "record" )
        {
            startRecordTest();
            return;
        }
        if( juce::SystemStats::getEnvironmentVariable( "KICKEM_SELFTEST", {} ) == "modular" )
        {
            startModularTest();
            return;
        }

        wait( 4 );
        note( "jog ENTER (load selected project)" );
        s.push_back( [this] { mainControl.getJog().pushCentre( true ); } );
        s.push_back( [this] { mainControl.getJog().pushCentre( false ); } );
        wait( 12 );
        note( "pads 1 and 6" );
        pad( 0 );
        pad( 5 );
        note( "bank B" );
        click( DCC::EN_BANK_B );
        wait( 2 );
        note( "SHIFT + bank C (-> bank G)" );
        s.push_back( [this] { mainControl.getShiftButton().press( true ); } );
        s.push_back( [this] { mainControl.getShiftButton().press( false ); } );
        click( DCC::EN_BANK_C );
        wait( 2 );
        note( "bank A" );
        click( DCC::EN_BANK_A );
        note( "PLAY" );
        click( DCC::EN_PLAY );
        wait( 3 );
        note( "STOP" );
        click( DCC::EN_STOP );
        note( "jog +3 / -2" );
        s.push_back( [this] { mainControl.getJog().turn( 3 ); } );
        s.push_back( [this] { mainControl.getJog().turn( -2 ); } );
        note( "Q-link P1..P4 +2 / -1" );
        for( int q = 0; q < 4; ++q )
        {
            s.push_back( [this, q] { qlinks[(size_t)q]->turn( 2 ); } );
            s.push_back( [this, q] { qlinks[(size_t)q]->turn( -1 ); } );
        }
        note( "slider 30 / 70" );
        s.push_back( [this] { slider.setTouch( 30 ); } );
        s.push_back( [this] { slider.setTouch( 70 ); } );
        note( "LINK, ERASE, MAIN" );
        click( DCC::EN_LINK );
        click( DCC::EN_ERASE );
        click( DCC::EN_MAINMODE );
        note( "done" );

        startTimer( 400 );
    }

    void SimulatorBoard::startInstrumentTest()
    {
        // KICKEM_SELFTEST=instrument: create an instrument program, open its edit screen,
        // drive Q-links and pads, then quit. KICKEM_SHOT=<folder> saves screen snapshots.
        auto& s    = selfTestSteps;
        auto  log  = []( juce::String text ) { SimulatorInterfaceImpl::log( "SELFTEST " + text ); };
        auto  wait = [&s]( int n ) { for( int i = 0; i < n; ++i ) s.push_back( [] {} ); };
        auto  click = [&s, this]( int code ) {
            s.push_back( [this, code] { if( auto* b = findButton( code ) ) b->press( true ); } );
            s.push_back( [this, code] { if( auto* b = findButton( code ) ) b->press( false ); } );
        };
        auto shot = [&s, this, log]( juce::String name ) {
            s.push_back( [this, log, name] {
                const auto dir = juce::SystemStats::getEnvironmentVariable( "KICKEM_SHOT", {} );
                if( dir.isEmpty() )
                    return;
                auto file = juce::File( dir ).getChildFile( name + ".png" );
                file.deleteFile();
                if( auto out = file.createOutputStream() )
                {
                    juce::PNGImageFormat().writeImageToStream( screen.createComponentSnapshot( screen.getLocalBounds() ), *out );
                    log( "snapshot " + file.getFullPathName() );
                }
            } );
        };
        auto instrument = []() -> mt::MTInstrumentProgram* {
            auto& ae = mt::getAudioInterface();
            return dynamic_cast<mt::MTInstrumentProgram*>( ae.getProgramForTrack( ae.getSelectedSequence(), ae.getSelectedTrack() ) );
        };

        wait( 4 );
        // Work on a throwaway copy of a template: the app saves the open project on exit,
        // so loading the first project in the list would modify the user's own projects.
        s.push_back( [log] {
            auto exeDir  = juce::File::getSpecialLocation( juce::File::currentApplicationFile ).getParentDirectory();
            // The engine opens <folder>/<folder name>.kickPrj.
            auto source  = exeDir.getChildFile( "templates/Template 01/Template 01.kickPrj" );
            auto scratch = juce::File::getSpecialLocation( juce::File::tempDirectory ).getChildFile( "kickem_selftest/SelfTest" );
            scratch.getParentDirectory().deleteRecursively();
            scratch.createDirectory();
            const bool ok = source.copyFileTo( scratch.getChildFile( "SelfTest.kickPrj" ) ) && mt::getAudioInterface().loadProject( scratch );
            log( "loaded scratch project " + scratch.getFullPathName() + ( ok ? "" : " (FAILED)" ) );
        } );
        wait( 8 );

        s.push_back( [log] {
            auto& ae = mt::getAudioInterface();
            if( !ae.getNumSequences() )
                ae.createSequence();
            const auto seq  = ae.getSelectedSequence();
            const auto trk  = ae.createTrack( seq, "Synth" );
            const auto prog = ae.createProgram( mt::MTProgramType::Plugin, "Synth 1" );
            ae.setProgramToTrack( prog, seq, trk );
            ae.setSelectedSequenceAndTrack( seq, trk );
            ae.setSelectedProgram( prog );
            log( "created instrument program " + juce::String( prog ) + " on track " + juce::String( trk ) );
        } );
        wait( 2 );
        s.push_back( [log] { log( "PROGRAM EDIT" ); } );
        click( DCC::EN_PROG_EDIT );
        wait( 4 );
        s.push_back( [log] {
            log( "screen = " + juce::String( (int)kick_ui::KickHome::Instance().getCurrentScreen() ) + " (MenuProgramEdit = " + juce::String( (int)kick_ui::Screen::MenuProgramEdit ) + ")" );
        } );
        shot( "instrument_edit_page1" );

        s.push_back( [log, instrument] {
            if( auto* i = instrument() )
                log( "before: " + i->getParams()[0].name + "=" + juce::String( i->getParam( 0 ), 3 ) + "  " + i->getParams()[3].name + "=" + juce::String( i->getParam( 3 ), 1 ) );
        } );
        s.push_back( [log] { log( "Q-link P1 +20, P4 -30" ); } );
        s.push_back( [this] { qlinks[0]->turn( 20 ); } );
        s.push_back( [this] { qlinks[3]->turn( -30 ); } );
        wait( 2 );
        s.push_back( [log, instrument] {
            if( auto* i = instrument() )
                log( "after:  " + i->getParams()[0].name + "=" + juce::String( i->getParam( 0 ), 3 ) + "  " + i->getParams()[3].name + "=" + juce::String( i->getParam( 3 ), 1 ) );
        } );
        shot( "instrument_edit_after_qlinks" );

        s.push_back( [log] { log( "bank D, pads 1-4 (C3 D3 E3 F3 in C major 4ths)" ); } );
        click( DCC::EN_BANK_D );
        for( int p = 0; p < 4; ++p )
        {
            s.push_back( [this, p, log] {
                log( "pad " + juce::String( p + 1 ) + " -> note " + juce::String( kick_ui::PadNotes::noteForPad( p ) ) );
                pads.getPad( p ).hit( true, 100 );
            } );
            wait( 1 );
            s.push_back( [this, p] { pads.getPad( p ).hit( false ); } );
        }

        // Modular: pick it in the ENGINE box and a preset in the PRESET box, as a user would.
        auto listWith = [this]( juce::String Option ) -> ui::KickListBox* {
            std::function<ui::KickListBox*( juce::Component& )> find = [&find, &Option]( juce::Component& c ) -> ui::KickListBox* {
                if( auto* lb = dynamic_cast<ui::KickListBox*>( &c ); lb && lb->isShowing() )
                {
                    const auto options = lb->getListOptions();
                    if( std::find( options.begin(), options.end(), Option ) != options.end() )
                        return lb;
                }
                for( auto* child : c.getChildren() )
                    if( auto* hit = find( *child ) )
                        return hit;
                return nullptr;
            };
            return find( screen );
        };
        auto choose = [&s, listWith, log]( juce::String Option ) {
            s.push_back( [listWith, log, Option] {
                if( auto* lb = listWith( Option ) )
                {
                    lb->setSelected( Option, juce::sendNotification );
                    log( "chose " + Option );
                }
                else
                    log( "no list box offers " + Option );
            } );
        };
        auto report = [&s, log, instrument]() {
            s.push_back( [log, instrument] {
                if( auto* i = instrument() )
                    log( "engine " + i->getEngineName() + ", patch " + i->getPatchName() + ( i->getPatch().isValid() ? " (stored)" : " (default)" ) + ", " +
                         juce::String( (int)i->getParams().size() ) + " params, presets: " + i->getPresetNames().joinIntoString( ", " ) );
            } );
        };
        auto playPads = [&s, this, wait]() {
            for( int p = 0; p < 4; ++p )
            {
                s.push_back( [this, p] { pads.getPad( p ).hit( true, 100 ); } );
                wait( 1 );
                s.push_back( [this, p] { pads.getPad( p ).hit( false ); } );
            }
        };

        choose( "Modular" );
        wait( 3 );
        report();
        shot( "modular_edit_init" );
        playPads();
        choose( "FM Bell" );
        wait( 3 );
        report();
        shot( "modular_edit_fm_bell" );
        playPads();
        s.push_back( [log] { log( "Q-link P1 +40 (Macro 1)" ); } );
        s.push_back( [this] { qlinks[0]->turn( 40 ); } );
        wait( 2 );
        s.push_back( [log, instrument] {
            if( auto* i = instrument() )
                log( i->getParams()[0].name + "=" + juce::String( i->getParam( 0 ), 3 ) );
        } );
        playPads();
        shot( "modular_edit_macro" );
        s.push_back( [log] { log( "done" ); } );
        wait( 2 );
        s.push_back( [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); } );

        startTimer( 400 );
    }

    void SimulatorBoard::startModularTest()
    {
        auto& s     = selfTestSteps;
        auto  log   = []( juce::String text ) { SimulatorInterfaceImpl::log( "SELFTEST " + text ); };
        auto  wait  = [&s]( int n ) { for( int i = 0; i < n; ++i ) s.push_back( [] {} ); };
        auto  click = [&s, this]( int code ) {
            s.push_back( [this, code] { if( auto* b = findButton( code ) ) b->press( true ); } );
            s.push_back( [this, code] { if( auto* b = findButton( code ) ) b->press( false ); } );
        };
        auto shot = [&s, this, log]( juce::String name ) {
            s.push_back( [this, log, name] {
                const auto dir = juce::SystemStats::getEnvironmentVariable( "KICKEM_SHOT", {} );
                if( dir.isEmpty() )
                    return;
                auto file = juce::File( dir ).getChildFile( name + ".png" );
                file.deleteFile();
                if( auto out = file.createOutputStream() )
                {
                    juce::PNGImageFormat().writeImageToStream( screen.createComponentSnapshot( screen.getLocalBounds() ), *out );
                    log( "snapshot " + file.getFullPathName() );
                }
            } );
        };
        auto instrument = []() -> mt::MTInstrumentProgram* {
            auto& ae = mt::getAudioInterface();
            return dynamic_cast<mt::MTInstrumentProgram*>( ae.getProgramForTrack( ae.getSelectedSequence(), ae.getSelectedTrack() ) );
        };
        // Finds a showing component anywhere on screen, popups included.
        auto findShowing = [this]( std::function<bool( juce::Component& )> Match ) -> juce::Component* {
            std::function<juce::Component*( juce::Component& )> find = [&find, &Match]( juce::Component& c ) -> juce::Component* {
                if( c.isShowing() && Match( c ) )
                    return &c;
                for( auto* child : c.getChildren() )
                    if( auto* hit = find( *child ) )
                        return hit;
                return nullptr;
            };
            return find( *getTopLevelComponent() );
        };
        auto editor = [findShowing]() { return dynamic_cast<kick_ui::ModularEditor*>( findShowing( []( juce::Component& c ) { return dynamic_cast<kick_ui::ModularEditor*>( &c ) != nullptr; } ) ); };
        auto tapButton = [&s, findShowing, log]( juce::String Text ) {
            s.push_back( [findShowing, log, Text] {
                auto* b = dynamic_cast<juce::Button*>( findShowing( [Text]( juce::Component& c ) {
                    auto* btn = dynamic_cast<juce::Button*>( &c );
                    return btn && btn->getButtonText() == Text;
                } ) );
                if( b )
                    b->triggerClick();
                log( ( b ? "tapped " : "no button " ) + Text );
            } );
        };
        auto describe = [&s, log, instrument]( juce::String When ) {
            s.push_back( [log, instrument, When] {
                auto* i = instrument();
                if( !i )
                    return;
                const auto                     patch = i->getPatch();
                const mt::modular::PatchEditor read( patch, nullptr );
                juce::StringArray              parts;
                for( const auto& m : patch )
                {
                    juce::String d = read.moduleLabel( m.getProperty( "id" ) ) + "@" + m.getProperty( "col" ).toString() + "," + m.getProperty( "row" ).toString();
                    if( m.hasProperty( "cutoff" ) )
                        d << " cutoff=" << juce::String( (float)m.getProperty( "cutoff" ), 1 );
                    for( const auto& r : m )
                        d << " [" << r.getProperty( "param" ).toString() << "<" << read.sourceLabel( r.getProperty( "source" ) ) << " "
                          << juce::String( (float)r.getProperty( r.hasType( "CABLE" ) ? "amount" : "depth" ), 2 ) << "]";
                    parts.add( d );
                }
                log( When + ": " + ( patch.isValid() ? patch.getProperty( "name" ).toString() : juce::String( "(default)" ) ) + " | " + parts.joinIntoString( " | " ) );
            } );
        };

        wait( 4 );
        s.push_back( [log] {
            auto exeDir  = juce::File::getSpecialLocation( juce::File::currentApplicationFile ).getParentDirectory();
            auto source  = exeDir.getChildFile( "templates/Template 01/Template 01.kickPrj" );
            auto scratch = juce::File::getSpecialLocation( juce::File::tempDirectory ).getChildFile( "kickem_selftest/SelfTest" );
            scratch.getParentDirectory().deleteRecursively();
            scratch.createDirectory();
            const bool ok = source.copyFileTo( scratch.getChildFile( "SelfTest.kickPrj" ) ) && mt::getAudioInterface().loadProject( scratch );
            log( "loaded scratch project " + scratch.getFullPathName() + ( ok ? "" : " (FAILED)" ) );
        } );
        wait( 8 );
        s.push_back( [log] {
            auto& ae = mt::getAudioInterface();
            if( !ae.getNumSequences() )
                ae.createSequence();
            const auto seq  = ae.getSelectedSequence();
            const auto trk  = ae.createTrack( seq, "Modular" );
            const auto prog = ae.createProgram( mt::MTProgramType::Plugin, "Modular 1" );
            ae.setProgramToTrack( prog, seq, trk );
            ae.setSelectedSequenceAndTrack( seq, trk );
            ae.setSelectedProgram( prog );
            if( auto* i = dynamic_cast<mt::MTInstrumentProgram*>( ae.getProgram( prog ) ) )
                i->setEngine( "Modular" );
            log( "created Modular program " + juce::String( prog ) );
        } );
        wait( 2 );
        click( DCC::EN_PROG_EDIT );
        wait( 4 );
        tapButton( "EDIT PATCH" );
        wait( 3 );
        s.push_back( [log, editor] { log( editor() ? "patch editor open" : "patch editor NOT open" ); } );
        describe( "start" );
        shot( "modular_editor_init" );

        // Select the filter and turn Q-link P2 (cutoff) down.
        s.push_back( [editor] { if( auto* e = editor() ) e->selectCell( 0, 1 ); } );
        wait( 1 );
        s.push_back( [this] { qlinks[1]->turn( -25 ); } );
        wait( 2 );
        describe( "after P2 -25 on the filter" );
        shot( "modular_editor_filter" );

        // Add an LFO in an empty cell through the module list, as a tap would.
        s.push_back( [editor] { if( auto* e = editor() ) e->selectCell( 1, 1 ); } );
        tapButton( "+ MODULE" );
        wait( 3 );
        shot( "modular_editor_picker" );
        tapButton( "LFO" );
        wait( 3 );
        describe( "after adding an LFO" );

        // Modulate the filter cutoff from it: + MOD, pick the knob, then the source.
        s.push_back( [editor] { if( auto* e = editor() ) e->selectCell( 0, 1 ); } );
        wait( 1 );
        tapButton( "+ MOD" );
        wait( 3 );
        tapButton( "Cutoff" );
        wait( 4 );
        tapButton( "LFO 1" );
        wait( 3 );
        s.push_back( [this] { qlinks[2]->turn( -20 ); } ); // the new modulation is third: P3, depth 0.5 -> 0.1
        wait( 2 );
        describe( "after modulating cutoff from the LFO" );
        shot( "modular_editor_mods" );

        // Move the LFO with the API a drag uses, play, then undo the move.
        s.push_back( [editor] {
            if( auto* e = editor() )
            {
                e->selectCell( 1, 1 );
                e->moveModule( 3, 2 );
            }
        } );
        wait( 2 );
        describe( "after moving the LFO" );
        click( DCC::EN_BANK_D );
        for( int p = 0; p < 4; ++p )
        {
            s.push_back( [this, p] { pads.getPad( p ).hit( true, 100 ); } );
            wait( 1 );
            s.push_back( [this, p] { pads.getPad( p ).hit( false ); } );
        }
        s.push_back( [] {
            mt::getAudioInterface().getUndoManager().beginNewTransaction();
            mt::getAudioInterface().performUndo();
        } );
        wait( 2 );
        describe( "after undo" );
        shot( "modular_editor_after_undo" );

        // Milestone 3: an effect, a cable, and saving a user preset.
        s.push_back( [editor] { if( auto* e = editor() ) e->selectCell( 0, kick_ui::ModularEditor::effectRow ); } );
        wait( 1 );
        tapButton( "+ EFFECT" );
        wait( 3 );
        tapButton( "Reverb" );
        wait( 3 );
        s.push_back( [this] { qlinks[3]->turn( 30 ); } ); // P4 = mix
        wait( 2 );
        describe( "after adding a reverb" );
        shot( "modular_editor_effect" );

        s.push_back( [editor] { if( auto* e = editor() ) e->selectCell( 2, 0 ); } );
        tapButton( "+ MODULE" );
        wait( 3 );
        tapButton( "Oscillator" );
        wait( 3 );
        s.push_back( [editor] { if( auto* e = editor() ) e->selectCell( 0, 0 ); } );
        wait( 1 );
        tapButton( "IN (0)" );
        wait( 1 );
        tapButton( "+ CABLE" );
        wait( 3 );
        tapButton( "Oscillator 2" );
        wait( 3 );
        s.push_back( [this] { qlinks[0]->turn( 20 ); } ); // amount 0.5 -> 0.9 (2% of -1..1 per detent)
        wait( 2 );
        describe( "after cabling Oscillator 2 into Oscillator 1" );
        shot( "modular_editor_cable" );
        click( DCC::EN_BANK_D );
        for( int p = 0; p < 4; ++p )
        {
            s.push_back( [this, p] { pads.getPad( p ).hit( true, 100 ); } );
            wait( 1 );
            s.push_back( [this, p] { pads.getPad( p ).hit( false ); } );
        }

        tapButton( "SAVE" );
        wait( 3 );
        shot( "modular_editor_save" );
        tapButton( "Apply" );
        wait( 3 );
        s.push_back( [log, instrument] {
            if( auto* i = instrument() )
                log( "after SAVE: patch " + i->getPatchName() + ", presets: " + i->getPresetNames().joinIntoString( ", " ) + " | macro 1 = " + i->getParamName( 0 ) );
        } );

        // Round 4: legato with glide, pressure, live activity, moving an effect, deleting a preset.
        tapButton( "POLY 8" );
        wait( 3 );
        tapButton( "Legato" );
        wait( 4 );
        tapButton( "100 ms" );
        wait( 3 );
        s.push_back( [log, instrument] {
            if( auto* i = instrument() )
            {
                const mt::modular::PatchEditor read( i->getPatch(), nullptr );
                log( "play mode " + juce::String( read.getPlayMode() ) + ", glide " + juce::String( read.getGlide(), 3 ) + " s" );
            }
        } );
        s.push_back( [editor] {
            if( auto* e = editor() )
            {
                e->selectCell( 0, kick_ui::ModularEditor::effectRow );
                e->moveEffect( 2 );
            }
        } );
        wait( 2 );
        describe( "after moving the reverb to FX 3" );
        s.push_back( [this] { pads.getPad( 0 ).hit( true, 110 ); } );
        wait( 2 );
        s.push_back( [this] { pads.getPad( 0 ).press( 100 ); } );
        wait( 3 );
        s.push_back( [log, instrument] {
            if( auto* i = instrument() )
            {
                juce::StringArray parts;
                for( const auto& m : i->getPatch() )
                    parts.add( m.getProperty( "type" ).toString() + "=" + juce::String( i->getActivity( m.getProperty( "id" ) ), 2 ) );
                log( "activity while held: " + parts.joinIntoString( " " ) );
            }
        } );
        shot( "modular_editor_activity" );
        s.push_back( [this] { pads.getPad( 0 ).hit( false ); } );
        wait( 2 );

        tapButton( "SAVE" );
        wait( 3 );
        tapButton( "Delete a user preset" );
        wait( 4 );
        tapButton( "Init 2" );
        wait( 3 );
        s.push_back( [log, instrument] {
            if( auto* i = instrument() )
                log( "after delete: presets " + i->getPresetNames().joinIntoString( ", " ) );
        } );

        // Wave 5: a Formant module from the list, and the arpeggiator set up through ARP.
        s.push_back( [editor] { if( auto* e = editor() ) e->selectCell( 3, 0 ); } );
        tapButton( "+ MODULE" );
        wait( 3 );
        tapButton( "Formant" );
        wait( 3 );
        tapButton( "ARP OFF" );
        wait( 3 );
        tapButton( "Up" );
        wait( 4 );
        tapButton( "1/16" );
        wait( 4 );
        tapButton( "2" );
        wait( 4 );
        tapButton( "50%" );
        wait( 3 );
        s.push_back( [log, instrument] {
            if( auto* i = instrument() )
            {
                const mt::modular::PatchEditor read( i->getPatch(), nullptr );
                log( "arp mode " + juce::String( read.getArp() ) + ", rate " + juce::String( read.getArpRate() ) + ", octaves " + juce::String( read.getArpOctaves() ) + ", gate " +
                     juce::String( read.getArpGate(), 2 ) + " | modules: " + juce::String( i->getPatch().getNumChildren() ) );
            }
        } );
        for( int p : { 0, 2, 4 } ) // a chord: three pads together
            s.push_back( [this, p] { pads.getPad( p ).hit( true, 100 ); } );
        wait( 3 );
        shot( "modular_editor_arp" );
        wait( 2 );
        for( int p : { 0, 2, 4 } )
            s.push_back( [this, p] { pads.getPad( p ).hit( false ); } );
        wait( 2 );

        tapButton( "< BACK" );
        wait( 3 );
        s.push_back( [log, instrument] {
            if( auto* i = instrument() )
                log( "back on the instrument page, preset box shows: " + i->getPatchName() );
        } );
        shot( "modular_editor_closed" );
        s.push_back( [log] { log( "done" ); } );
        wait( 2 );
        s.push_back( [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); } );

        startTimer( 400 );
    }

    void SimulatorBoard::startAudioSettingsTest()
    {
        // KICKEM_SELFTEST=audio: list device types, open Settings > Audio, snapshot, quit.
        auto& s   = selfTestSteps;
        auto  log = []( juce::String text ) { SimulatorInterfaceImpl::log( "SELFTEST " + text ); };
        auto  wait = [&s]( int n ) { for( int i = 0; i < n; ++i ) s.push_back( [] {} ); };

        wait( 6 ); // the engine opens its device asynchronously
        s.push_back( [log] {
            auto& dm = mt::getAudioInterface().getAudioDeviceManager();
            for( auto* type : dm.getAvailableDeviceTypes() )
            {
                type->scanForDevices();
                log( "device type " + type->getTypeName() + ": " + type->getDeviceNames().joinIntoString( " | " ) );
            }
            auto* dev = dm.getCurrentAudioDevice();
            log( "current: " + dm.getCurrentAudioDeviceType() + " / " + ( dev ? dev->getName() + " @ " + juce::String( dev->getCurrentSampleRate() ) + " Hz, " + juce::String( dev->getCurrentBufferSizeSamples() ) : juce::String( "none" ) ) );
        } );
        s.push_back( [] { kick_ui::signals::sigScreenClose( kick_ui::Screen::HomePreferencesView ); } );
        wait( 3 );
        s.push_back( [this, log] {
            // Click the "Audio" tab the way a user would.
            std::function<juce::Button*( juce::Component& )> find = [&find]( juce::Component& c ) -> juce::Button* {
                if( auto* b = dynamic_cast<juce::Button*>( &c ); b && b->getName() == "Audio" )
                    return b;
                for( auto* child : c.getChildren() )
                    if( auto* hit = find( *child ) )
                        return hit;
                return nullptr;
            };
            if( auto* tab = find( screen ) )
            {
                tab->triggerClick();
                log( "clicked Audio tab" );
            }
            else
                log( "Audio tab not found" );
        } );
        wait( 4 );
        s.push_back( [this, log] {
            const auto dir = juce::SystemStats::getEnvironmentVariable( "KICKEM_SHOT", {} );
            if( dir.isEmpty() )
                return;
            auto file = juce::File( dir ).getChildFile( "settings_audio.png" );
            file.deleteFile();
            if( auto out = file.createOutputStream() )
            {
                juce::PNGImageFormat().writeImageToStream( screen.createComponentSnapshot( screen.getLocalBounds() ), *out );
                log( "snapshot " + file.getFullPathName() );
            }
        } );
        s.push_back( [log] { log( "done" ); } );
        wait( 2 );
        s.push_back( [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); } );

        startTimer( 400 );
    }

    void SimulatorBoard::startTourTest()
    {
        // KICKEM_SELFTEST=tour: load a scratch copy of a kit (KICKEM_TOUR_PROJECT=<project name>,
        // default "Vintage Soul Kit"), then press every panel button and its SHIFT function from
        // the main screen. Each stop logs the screen the app reports and, with KICKEM_SHOT, saves a snapshot.
        auto& s    = selfTestSteps;
        auto  log  = []( juce::String text ) { SimulatorInterfaceImpl::log( "SELFTEST " + text ); };
        auto  wait = [&s]( int n ) { for( int i = 0; i < n; ++i ) s.push_back( [] {} ); };
        auto  screenName = []() -> juce::String {
            static const char* names[] = { "HomeOptions", "HomeListView", "HomeGridView", "HomeSequencerView", "HomeXYView", "HomePreferencesView",
                                           "HomePreferences", "MenuSampleEdit", "MenuProgramEdit", "MenuSampler", "MenuLooper", "MenuPadMute",
                                           "MenuTrackMute", "MenuPadMixer", "MenuChannelMixer", "MenuKnobEdit", "MenuListEdit", "MenuPadPerform",
                                           "MenuMidiControl", "MenuFileBrowser", "MenuNextSequence", "MenuSong", "MenuColorEdit", "MenuAssignSample",
                                           "RecentProject", "EditProgram", "SWUpdateView", "SWUpdateWindow", "ProcessEdit", "CVClib",
                                           "SystemResources", "Inserts", "Temp" };
            const int i = (int)kick_ui::KickHome::Instance().getCurrentScreen() - (int)kick_ui::Screen::HomeOptions;
            return i >= 0 && i < (int)std::size( names ) ? juce::String( names[i] ) : juce::String( i + (int)kick_ui::Screen::HomeOptions );
        };
        auto shot = [&s, this, log]( juce::String name ) {
            s.push_back( [this, log, name] {
                const auto dir = juce::SystemStats::getEnvironmentVariable( "KICKEM_SHOT", {} );
                if( dir.isEmpty() )
                    return;
                auto file = juce::File( dir ).getChildFile( name + ".png" );
                file.deleteFile();
                if( auto out = file.createOutputStream() )
                    juce::PNGImageFormat().writeImageToStream( screen.createComponentSnapshot( screen.getLocalBounds() ), *out );
            } );
        };
        auto press = [&s, this]( int code, bool shifted ) {
            s.push_back( [this, code, shifted] {
                if( auto* b = findButton( code ) )
                    b->press( true, shifted );
            } );
            s.push_back( [this, code] {
                if( auto* b = findButton( code ) )
                    b->press( false );
            } );
        };
        auto visit = [&]( juce::String label, int code, bool shifted ) {
            s.push_back( [log, label] { log( "press " + label ); } );
            press( code, shifted );
            wait( 4 );
            s.push_back( [log, label, screenName] { log( "  " + label + " -> screen " + screenName() ); } );
            shot( "tour_" + label.replace( " ", "_" ).replace( "/", "_" ) );
            // Poke the screen's controls: every Q-link, the jog and a pad. Preferences are global
            // (not part of the scratch project), so leave that screen alone.
            if( label != "PREFS" )
            {
                for( int q = 0; q < 4; ++q )
                {
                    s.push_back( [this, q] { qlinks[(size_t)q]->turn( 1 ); } );
                    s.push_back( [this, q] { qlinks[(size_t)q]->turn( -1 ); } );
                }
                s.push_back( [this] { mainControl.getJog().turn( 1 ); } );
                s.push_back( [this] { mainControl.getJog().turn( -1 ); } );
                s.push_back( [this] { pads.getPad( 0 ).hit( true, 100 ); } );
                s.push_back( [this] { pads.getPad( 0 ).hit( false ); } );
                s.push_back( [log, label, screenName] { log( "  " + label + " controls ok, screen " + screenName() ); } );
            }
            // Back to the main screen for the next stop.
            press( DCC::EN_MAINMODE, false );
            wait( 3 );
        };

        wait( 4 );
        s.push_back( [log] {
            auto exeDir = juce::File::getSpecialLocation( juce::File::currentApplicationFile ).getParentDirectory();
            auto name   = juce::SystemStats::getEnvironmentVariable( "KICKEM_TOUR_PROJECT", "Vintage Soul Kit" );
            auto source = exeDir.getChildFile( "projects" ).getChildFile( name );
            if( !source.isDirectory() )
                source = exeDir.getChildFile( "templates/Template 01" );
            // The engine opens <folder>/<folder name>.kickPrj, so keep the project's folder name.
            auto root = juce::File::getSpecialLocation( juce::File::tempDirectory ).getChildFile( "kickem_tour" );
            root.deleteRecursively();
            auto       scratch = root.getChildFile( source.getFileName() );
            const bool ok      = source.copyDirectoryTo( scratch ) && mt::getAudioInterface().loadProject( scratch );
            auto&      ae      = mt::getAudioInterface();
            log( "loaded scratch copy of " + source.getFileName() + ( ok ? "" : " (FAILED)" ) + ": " + juce::String( ae.getNumSequences() ) + " sequence(s), "
                 + juce::String( ae.getNumPrograms() ) + " program(s)" );
        } );
        wait( 8 );
        press( DCC::EN_MAINMODE, false );
        wait( 3 );
        s.push_back( [log, screenName] {
            auto& ae = mt::getAudioInterface();
            log( "main screen = " + screenName() + ", sequences " + juce::String( ae.getNumSequences() ) );
        } );
        shot( "tour_MAIN" );

        // Transport from the main screen.
        s.push_back( [log] { log( "press PLAY" ); } );
        press( DCC::EN_PLAY, false );
        wait( 5 );
        s.push_back( [log] { log( juce::String( "  playing = " ) + ( mt::getAudioInterface().isSequencePlaying() ? "yes" : "no" ) ); } );
        shot( "tour_PLAYING" );
        s.push_back( [log] { log( "press STOP" ); } );
        press( DCC::EN_STOP, false );
        wait( 2 );
        s.push_back( [log] { log( juce::String( "  playing = " ) + ( mt::getAudioInterface().isSequencePlaying() ? "yes" : "no" ) ); } );

        struct Stop
        {
            const char* label;
            int         code;
            bool        shifted;
        };
        const Stop stops[] = {
            { "GRID", DCC::EN_MAINMODE, true },          { "BROWSE", DCC::EN_BROWSE, false },
            { "TRACK MIX", DCC::EN_TRACK_MIX, false },   { "PAD MIX", DCC::EN_TRACK_MIX, true },
            { "TRACK MUTE", DCC::EN_TRACK_MUTE, false }, { "PAD MUTE", DCC::EN_TRACK_MUTE, true },
            { "NEXT SEQ", DCC::EN_NEXT_SEQ, false },     { "FX", DCC::EN_NEXT_SEQ, true },
            { "STEP SEQ", DCC::EN_STEP_SEQ, false },     { "AUTOMATION", DCC::EN_STEP_SEQ, true },
            { "TAP TEMPO", DCC::EN_TAP_TEMPO, false },   { "MASTER", DCC::EN_TAP_TEMPO, true },
            { "NOTE REPEAT", DCC::EN_NOTE_REPEAT, false }, { "LATCH", DCC::EN_NOTE_REPEAT, true },
            { "TC", DCC::EN_TC, false },                 { "ON-OFF", DCC::EN_TC, true },
            { "SAMPLER", DCC::EN_SAMPLER, false },       { "LOOPER", DCC::EN_SAMPLER, true },
            { "SAMPLE EDIT", DCC::EN_SAMPLE_EDIT, false }, { "LINK EDIT", DCC::EN_SAMPLE_EDIT, true },
            { "PROGRAM EDIT", DCC::EN_PROG_EDIT, false }, { "MIDI CONTROL", DCC::EN_PROG_EDIT, true },
            { "MENU", DCC::EN_MENU, false },             { "PREFS", DCC::EN_MENU, true },
            { "FULL LEVEL", DCC::EN_FULL_LEVEL, false }, { "HALF LEVEL", DCC::EN_FULL_LEVEL, true },
            { "16 LEVELS", DCC::EN_16LEVELS, false },    { "NOTES", DCC::EN_16LEVELS, true },
            { "COPY", DCC::EN_COPY, false },             { "LINK", DCC::EN_LINK, false },
            { "REC", DCC::EN_RECORD, false },            { "OVERDUB", DCC::EN_OVERDUB, false },
            { "PLAY START", DCC::EN_PLAYSTART, false },  { "STOP", DCC::EN_STOP, false },
        };
        // KICKEM_TOUR_FROM=<label> starts the button tour at that stop.
        auto from    = juce::SystemStats::getEnvironmentVariable( "KICKEM_TOUR_FROM", {} );
        bool started = from.isEmpty();
        for( const auto& stop : stops )
        {
            started = started || from == stop.label;
            if( started )
                visit( stop.label, stop.code, stop.shifted );
        }

        s.push_back( [log] { log( "done" ); } );
        wait( 2 );
        s.push_back( [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); } );

        startTimer( 400 );
    }

    void SimulatorBoard::startPopupTest()
    {
        auto& s    = selfTestSteps;
        auto  log  = []( juce::String text ) { SimulatorInterfaceImpl::log( "SELFTEST " + text ); };
        auto  wait = [&s]( int n ) { for( int i = 0; i < n; ++i ) s.push_back( [] {} ); };
        auto  click = [&s, this]( int code ) {
            s.push_back( [this, code] { if( auto* b = findButton( code ) ) b->press( true ); } );
            s.push_back( [this, code] { if( auto* b = findButton( code ) ) b->press( false ); } );
        };
        auto windowShot = [&s, this, log]( juce::String name ) {
            s.push_back( [this, log, name] {
                const auto dir = juce::SystemStats::getEnvironmentVariable( "KICKEM_SHOT", {} );
                auto*      top = getTopLevelComponent();
                if( dir.isEmpty() || !top )
                    return;
                auto file = juce::File( dir ).getChildFile( name + ".png" );
                file.deleteFile();
                if( auto out = file.createOutputStream() )
                    juce::PNGImageFormat().writeImageToStream( top->createComponentSnapshot( top->getLocalBounds() ), *out );
                log( "window snapshot " + file.getFullPathName() );
            } );
        };

        wait( 4 );
        s.push_back( [log] {
            auto exeDir = juce::File::getSpecialLocation( juce::File::currentApplicationFile ).getParentDirectory();
            auto source = exeDir.getChildFile( "projects/Vintage Soul Kit" );
            auto root   = juce::File::getSpecialLocation( juce::File::tempDirectory ).getChildFile( "kickem_popup" );
            root.deleteRecursively();
            auto scratch = root.getChildFile( source.getFileName() );
            const bool ok = source.copyDirectoryTo( scratch ) && mt::getAudioInterface().loadProject( scratch );
            log( juce::String( "loaded scratch kit" ) + ( ok ? "" : " (FAILED)" ) );
        } );
        wait( 8 );
        click( DCC::EN_MAINMODE ); // the Main screen's track row has list boxes (I/P MIDI PORT, channel)
        wait( 4 );
        s.push_back( [this, log] {
            std::function<ui::KickListBox*( juce::Component& )> find = [&find]( juce::Component& c ) -> ui::KickListBox* {
                if( auto* l = dynamic_cast<ui::KickListBox*>( &c ); l && l->isShowing() && l->isEnabled() )
                    return l;
                for( auto* child : c.getChildren() )
                    if( auto* hit = find( *child ) )
                        return hit;
                return nullptr;
            };
            if( auto* l = find( screen ) )
            {
                log( "opening list '" + l->getName() + "'" );
                l->knobClicked();
            }
            else
            {
                // Every pop-up goes through the same dialog code, so any content will do.
                log( "no list box showing; opening a dialog through mt::utils::showOptions" );
                auto list = std::make_unique<ui::KickList>( "Test", std::vector<juce::String> { "One", "Two", "Three" } );
                list->show( "TEST" );
                popupTestContent = std::move( list );
            }
        } );
        wait( 4 );
        windowShot( "popup_window" );
        s.push_back( [this, log] {
            auto* top = getTopLevelComponent();
            log( "window " + top->getLocalBounds().toString() + ", KICK screen in window " + top->getLocalArea( &screen, screen.getLocalBounds() ).toString() );
            for( auto* c : top->getChildren() )
                log( "  window child " + juce::String( typeid( *c ).name() ) + " bounds " + c->getBounds().toString() + ( c->isVisible() ? " visible" : "" ) );
            if( auto* modal = juce::Component::getCurrentlyModalComponent() )
            {
                log( "modal " + juce::String( typeid( *modal ).name() ) + " on desktop=" + juce::String( (int)modal->isOnDesktop() ) + " bounds in window "
                     + top->getLocalArea( modal, modal->getLocalBounds() ).toString() );
                for( auto* c : modal->getChildren() )
                    if( c->isVisible() )
                    {
                        auto r   = top->getLocalArea( c, c->getLocalBounds() );
                        auto hit = top->getComponentAt( r.getCentre() );
                        log( "  content " + juce::String( typeid( *c ).name() ) + " at " + r.toString() + "; a click at its centre reaches "
                             + ( hit ? juce::String( typeid( *hit ).name() ) : juce::String( "nothing" ) ) );
                    }
            }
            else
                log( "no modal component" );
            if( auto* modal = juce::Component::getCurrentlyModalComponent() )
                log( juce::String( "panel PLAY button clickable while open: " ) + ( modal->canModalEventBeSentToComponent( findButton( DCC::EN_PLAY ) ) ? "yes" : "no" )
                     + ", screen behind the dialog clickable: " + ( modal->canModalEventBeSentToComponent( &screen ) ? "yes" : "no" ) );
        } );
        // Drive the open list from the panel: jog +1 then ENTER should pick "Two" and close it.
        s.push_back( [this] { mainControl.getJog().turn( 1 ); } );
        wait( 1 );
        s.push_back( [this] { mainControl.getJog().pushCentre( true ); } );
        s.push_back( [this] { mainControl.getJog().pushCentre( false ); } );
        wait( 2 );
        s.push_back( [this, log] {
            auto* list = dynamic_cast<ui::KickList*>( popupTestContent.get() );
            log( "after jog +1 / ENTER: selected " + ( list ? list->getSelected() : juce::String( "?" ) ) + ", dialog "
                 + ( juce::Component::getCurrentlyModalComponent() ? "still open" : "closed" ) );
        } );
        s.push_back( [] { juce::Component::getCurrentlyModalComponent() ? juce::Component::getCurrentlyModalComponent()->exitModalState( 0 ) : void(); } );
        wait( 2 );
        s.push_back( [log] { log( "done" ); } );
        s.push_back( [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); } );
        startTimer( 400 );
    }

    void SimulatorBoard::startPickerTest()
    {
        auto& s    = selfTestSteps;
        auto  log  = []( juce::String text ) { SimulatorInterfaceImpl::log( "SELFTEST " + text ); };
        auto  wait = [&s]( int n ) { for( int i = 0; i < n; ++i ) s.push_back( [] {} ); };
        auto  click = [&s, this]( int code ) {
            s.push_back( [this, code] { if( auto* b = findButton( code ) ) b->press( true ); } );
            s.push_back( [this, code] { if( auto* b = findButton( code ) ) b->press( false ); } );
        };
        // All components of a type under Root, in tree order.
        auto findAll = []( juce::Component* root, auto* typeTag ) {
            using T = std::remove_pointer_t<decltype( typeTag )>;
            std::vector<T*>                        found;
            std::function<void( juce::Component* )> walk = [&]( juce::Component* c ) {
                if( auto* t = dynamic_cast<T*>( c ) )
                    found.push_back( t );
                for( auto* child : c->getChildren() )
                    walk( child );
            };
            if( root )
                walk( root );
            return found;
        };

        wait( 4 );
        s.push_back( [log] {
            auto exeDir = juce::File::getSpecialLocation( juce::File::currentApplicationFile ).getParentDirectory();
            auto source = exeDir.getChildFile( "projects/Trap Kit" );
            auto root   = juce::File::getSpecialLocation( juce::File::tempDirectory ).getChildFile( "kickem_picker" );
            root.deleteRecursively();
            auto       scratch = root.getChildFile( source.getFileName() );
            const bool ok      = source.copyDirectoryTo( scratch ) && mt::getAudioInterface().loadProject( scratch );
            log( juce::String( "loaded scratch Trap Kit" ) + ( ok ? "" : " (FAILED)" ) );
        } );
        wait( 8 );
        click( DCC::EN_MAINMODE );
        wait( 4 );
        s.push_back( [this, log, findAll] {
            // The track row's program picker: its icon button opens the "Select Program" dialog.
            auto pickers = findAll( &screen, (kick_ui::KickProgramSelectorWithDisplay*)nullptr );
            for( auto* p : pickers )
                if( p->isShowing() )
                    for( auto* b : findAll( p, (ui::KickImageButton*)nullptr ) )
                        if( b->getName() == "Program" )
                        {
                            log( "opening the track's program picker" );
                            b->triggerClick();
                            return;
                        }
            log( "no program picker showing (" + juce::String( (int)pickers.size() ) + " found)" );
        } );
        wait( 4 );
        s.push_back( [this, log, findAll] {
            auto* modal = juce::Component::getCurrentlyModalComponent();
            auto  rows  = findAll( modal, (kick_ui::ProgramSelectorDisplay*)nullptr );
            auto  table = findAll( modal, (ui::KickTableView*)nullptr );
            if( rows.size() < 5 || table.empty() )
            {
                log( "picker dialog not found" );
                return;
            }
            // Click the centre of the Plugin row (row 4) the way the canvas forwards a real click:
            // the event's position is in the row component, which the simulator draws scaled.
            auto* row = rows[4];
            auto  src = juce::Desktop::getInstance().getMainMouseSource();
            auto  pos = row->getLocalBounds().getCentre().toFloat();
            juce::MouseEvent e( src, pos, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, row, row, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false );
            static_cast<juce::Component*>( table[0] )->mouseDown( e );
            log( "clicked the Plugin row; table selected row " + juce::String( table[0]->getSelectedRow() ) + " (expected 4)" );
        } );
        wait( 3 );
        s.push_back( [log, findAll] {
            auto* modal = juce::Component::getCurrentlyModalComponent();
            for( auto* b : findAll( modal, (ui::KickToggleButton*)nullptr ) )
                if( b->getButtonText() == "+ New Plugin" && b->isShowing() )
                {
                    log( "picking '+ New Plugin'" );
                    b->triggerClick();
                    return;
                }
            log( "'+ New Plugin' option not shown" );
        } );
        wait( 2 );
        s.push_back( [log, findAll] {
            auto* modal = juce::Component::getCurrentlyModalComponent();
            for( auto* b : findAll( modal, (ui::KickImageButton*)nullptr ) )
                if( b->getName() == "Apply" && b->isShowing() )
                {
                    log( "Apply" );
                    b->triggerClick();
                    return;
                }
            log( "Apply button not found" );
        } );
        wait( 4 );
        s.push_back( [log] {
            auto& ae   = mt::getAudioInterface();
            auto* prog = ae.getProgramDataForTrack( ae.getSelectedSequence(), ae.getSelectedTrack() );
            log( "track now plays " + ( prog ? prog->name.get() + " (" + mt::toString( prog->type.get() ) + ")" : juce::String( "nothing" ) ) );
        } );
        s.push_back( [log] { log( "done" ); } );
        wait( 1 );
        s.push_back( [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); } );
        startTimer( 400 );
    }

    void SimulatorBoard::startResourcesTest()
    {
        auto& s    = selfTestSteps;
        auto  log  = []( juce::String text ) { SimulatorInterfaceImpl::log( "SELFTEST " + text ); };
        auto  wait = [&s]( int n ) { for( int i = 0; i < n; ++i ) s.push_back( [] {} ); };
        auto  click = [&s, this]( int code ) {
            s.push_back( [this, code] { if( auto* b = findButton( code ) ) b->press( true ); } );
            s.push_back( [this, code] { if( auto* b = findButton( code ) ) b->press( false ); } );
        };

        wait( 4 );
        s.push_back( [log] {
            auto exeDir = juce::File::getSpecialLocation( juce::File::currentApplicationFile ).getParentDirectory();
            auto source = exeDir.getChildFile( "projects/Vintage Soul Kit" );
            auto root   = juce::File::getSpecialLocation( juce::File::tempDirectory ).getChildFile( "kickem_resources" );
            root.deleteRecursively();
            auto       scratch = root.getChildFile( source.getFileName() );
            const bool ok      = source.copyDirectoryTo( scratch ) && mt::getAudioInterface().loadProject( scratch );
            log( juce::String( "loaded scratch kit" ) + ( ok ? "" : " (FAILED)" ) );
        } );
        wait( 8 );
        click( DCC::EN_MAINMODE );
        wait( 3 );
        click( DCC::EN_PLAY );
        wait( 2 );
        // GRID (SHIFT + MAIN): its header has the chip button; Main's top bar doesn't.
        s.push_back( [this] { if( auto* b = findButton( DCC::EN_MAINMODE ) ) b->press( true, true ); } );
        s.push_back( [this] { if( auto* b = findButton( DCC::EN_MAINMODE ) ) b->press( false ); } );
        wait( 3 );
        s.push_back( [this, log] {
            // Open the page the way the header's chip button does.
            std::function<ui::KickImageButton*( juce::Component& )> find = [&find]( juce::Component& c ) -> ui::KickImageButton* {
                if( auto* b = dynamic_cast<ui::KickImageButton*>( &c ); b && b->getName() == "Resources" && b->isShowing() )
                    return b;
                for( auto* child : c.getChildren() )
                    if( auto* hit = find( *child ) )
                        return hit;
                return nullptr;
            };
            if( auto* b = find( screen ) )
            {
                log( "opening System Resources" );
                b->triggerClick();
            }
            else
                log( "Resources button not found" );
        } );
        // The page refreshes once a second; let it take a few readings while the kit plays.
        wait( 10 );
        s.push_back( [this, log] {
            std::function<void( juce::Component& )> dump = [&]( juce::Component& c ) {
                if( auto* sl = dynamic_cast<ui::KickSlider*>( &c ); sl && sl->isShowing() )
                    log( "  " + sl->getName() + " = " + juce::String( sl->getValue(), 1 ) + " %" );
                for( auto* child : c.getChildren() )
                    dump( *child );
            };
            if( auto* modal = juce::Component::getCurrentlyModalComponent() )
                dump( *modal );
            log( juce::String( "playing = " ) + ( mt::getAudioInterface().isSequencePlaying() ? "yes" : "no" ) );
            const auto dir = juce::SystemStats::getEnvironmentVariable( "KICKEM_SHOT", {} );
            if( dir.isNotEmpty() )
                if( auto out = juce::File( dir ).getChildFile( "system_resources.png" ).createOutputStream() )
                    juce::PNGImageFormat().writeImageToStream( screen.createComponentSnapshot( screen.getLocalBounds() ), *out );
        } );
        // Leave time for an outside reading of this process before quitting.
        wait( 8 );
        click( DCC::EN_STOP );
        s.push_back( [] { juce::Component::getCurrentlyModalComponent() ? juce::Component::getCurrentlyModalComponent()->exitModalState( 0 ) : void(); } );
        s.push_back( [log] { log( "done" ); } );
        wait( 1 );
        s.push_back( [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); } );
        startTimer( 400 );
    }

    void SimulatorBoard::startRecordTest()
    {
        auto& s    = selfTestSteps;
        auto  log  = []( juce::String text ) { SimulatorInterfaceImpl::log( "SELFTEST " + text ); };
        auto  wait = [&s]( int n ) { for( int i = 0; i < n; ++i ) s.push_back( [] {} ); };
        auto  click = [&s, this]( int code ) {
            s.push_back( [this, code] { if( auto* b = findButton( code ) ) b->press( true ); } );
            s.push_back( [this, code] { if( auto* b = findButton( code ) ) b->press( false ); } );
        };
        // Starts of the selected track's notes on Note.
        auto starts = []( int Note ) {
            auto&                    ae = mt::getAudioInterface();
            std::vector<mt::MTNote>  notes;
            std::vector<int>         result;
            ae.getNotesInRange( ae.getSelected(), notes, mt::NotesInRange( Note, Note ) );
            for( auto& n : notes )
                result.push_back( n.start.get() );
            std::sort( result.begin(), result.end() );
            return result;
        };
        static std::vector<int> before; // shared between steps
        constexpr int           pad  = 2; // bank A pad 3 = note 2
        constexpr int           note = 2;

        wait( 4 );
        s.push_back( [log] {
            auto exeDir = juce::File::getSpecialLocation( juce::File::currentApplicationFile ).getParentDirectory();
            auto source = exeDir.getChildFile( "projects/Trap Kit" );
            auto root   = juce::File::getSpecialLocation( juce::File::tempDirectory ).getChildFile( "kickem_record" );
            root.deleteRecursively();
            auto       scratch = root.getChildFile( source.getFileName() );
            const bool ok      = source.copyDirectoryTo( scratch ) && mt::getAudioInterface().loadProject( scratch );
            log( juce::String( "loaded scratch Trap Kit" ) + ( ok ? "" : " (FAILED)" ) );
        } );
        wait( 8 );
        click( DCC::EN_MAINMODE );
        wait( 3 );
        s.push_back( [log, starts] {
            auto& ae = mt::getAudioInterface();
            before   = starts( note );
            auto* tm = ae.getTempoMgr( ae.getSelectedSequence() );
            log( "note " + juce::String( note ) + " has " + juce::String( (int)before.size() ) + " events before recording; grid "
                 + juce::String( tm ? tm->getTicksPerQuantization() : 0 ) + " ticks, " + juce::String( tm ? tm->getBPM() : 0 ) + " BPM" );
        } );
        s.push_back( [log] { log( "press REC" ); } );
        click( DCC::EN_RECORD );
        wait( 1 );
        s.push_back( [this, log] {
            auto& ae = mt::getAudioInterface();
            log( juce::String( "recording = " ) + ( ae.isSequenceRecording() ? "yes" : "no" ) + ", playing = " + ( ae.isSequencePlaying() ? "yes" : "no" )
                 + ", REC LED = " + ( findButton( DCC::EN_RECORD ) && findButton( DCC::EN_RECORD )->isLedOn() ? "on" : "off" ) );
        } );
        // Five hits, a little under a second apart (timer steps are 400 ms), held ~200-400 ms.
        for( int i = 0; i < 5; ++i )
        {
            s.push_back( [this] { pads.getPad( pad ).hit( true, 90 + 5 * 0 ); } );
            s.push_back( [this] { pads.getPad( pad ).hit( false ); } );
        }
        wait( 2 );
        s.push_back( [log] { log( "press STOP" ); } );
        click( DCC::EN_STOP );
        wait( 2 );
        s.push_back( [this, log, starts] {
            auto&      ae    = mt::getAudioInterface();
            const auto after = starts( note );
            auto*      tm    = ae.getTempoMgr( ae.getSelectedSequence() );
            const int  grid  = tm ? juce::jmax( 1, tm->getTicksPerQuantization() ) : 1;
            juce::StringArray added;
            int               offGrid = 0;
            for( auto t : after )
                if( std::find( before.begin(), before.end(), t ) == before.end() )
                {
                    added.add( juce::String( t ) );
                    offGrid += ( t % grid ) != 0;
                }
            log( "recording = " + juce::String( ae.isSequenceRecording() ? "yes" : "no" ) + ", REC LED = "
                 + ( findButton( DCC::EN_RECORD ) && findButton( DCC::EN_RECORD )->isLedOn() ? "on" : "off" ) );
            log( "note " + juce::String( note ) + " now has " + juce::String( (int)after.size() ) + " events; new at ticks [" + added.joinIntoString( ", " ) + "], "
                 + juce::String( offGrid ) + " off the grid" );
            ae.getUndoManager().undo();
            log( "after one undo: " + juce::String( (int)starts( note ).size() ) + " events (expected " + juce::String( (int)before.size() ) + ")" );
        } );

        // Sampler: panel REC arms, recording starts when the input crosses the threshold, REC again stops
        // (or cancels while still waiting for signal).
        auto inputLevel = [] {
            float peak = -100.f;
            for( auto& m : mt::getAudioInterface().getRecorderInputMeter() )
                peak = juce::jmax( peak, m.peak );
            return peak;
        };
        auto samplerState = [log, inputLevel]( juce::String when ) {
            auto& ae = mt::getAudioInterface();
            log( "sampler " + when + ": running = " + juce::String( ae.isRecorderRunning() ? "yes" : "no" ) + ", progress " + juce::String( ae.getRecorderProgress(), 3 )
                 + ", input peak " + juce::String( inputLevel(), 1 ) + " dB, threshold " + juce::String( ae.getRecorderThresholdDB(), 1 ) + " dB" );
        };
        s.push_back( [log] { log( "press SAMPLER, then REC" ); } );
        click( DCC::EN_SAMPLER );
        wait( 3 );
        click( DCC::EN_RECORD );
        wait( 5 );
        s.push_back( [samplerState] { samplerState( "2 s after REC" ); } );
        s.push_back( [log, inputLevel] {
            auto& ae = mt::getAudioInterface();
            if( ae.getRecorderProgress() <= 0.0f )
                log( "nothing above the threshold yet; REC again should cancel the arm" );
        } );
        click( DCC::EN_RECORD );
        wait( 2 );
        s.push_back( [samplerState] { samplerState( "after second REC" ); } );
        // If the first take never triggered (quiet room), retry with the threshold at its minimum.
        s.push_back( [log] {
            auto& ae = mt::getAudioInterface();
            if( ae.getRecorderProgress() <= 0.0f )
            {
                log( "retrying with the threshold at -100 dB" );
                ae.setRecorderThresholdDB( -100.f );
            }
        } );
        click( DCC::EN_RECORD );
        wait( 5 );
        s.push_back( [samplerState] { samplerState( "2 s into retry" ); } );
        click( DCC::EN_RECORD );
        wait( 2 );
        s.push_back( [samplerState] { samplerState( "stopped" ); } );
        s.push_back( [log] { log( "done" ); } );
        wait( 1 );
        s.push_back( [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); } );
        startTimer( 400 );
    }

    void SimulatorBoard::timerCallback()
    {
        if( selfTestIndex < selfTestSteps.size() )
            selfTestSteps[selfTestIndex++]();
        else
            stopTimer();
    }

    SimulatorBoard::~SimulatorBoard()
    {
        ShiftLatch::onChange = nullptr;
        juce::Desktop::getInstance().removeGlobalMouseListener( &mouseTrace );
        // KickHome is a process-wide singleton: detach it, don't let it be destroyed with the board.
        removeChildComponent( &screen );
    }

    void SimulatorBoard::handleOutput( const CPktMgr::HWMsg_t& m )
    {
        const auto& d = m.data;
        switch( m.pktType )
        {
            case CPktMgr::EN_PKT_TYPE_LED:
                // EN_LED_SET_SINGLE_LED / EN_LED_BLINK_SINGLE : { id, state, colour }
                if( ( m.pktCmd == DCC::EN_LED_SET_SINGLE_LED || m.pktCmd == DCC::EN_LED_BLINK_SINGLE ) && d.size() >= 2 )
                    setButtonLed( d[0], d[1] != DCC::StateOff );
                break;

            case CPktMgr::EN_PKT_TYPE_PAD:
                pads.applyPadPacket( m );
                break;

            case CPktMgr::EN_PKT_TYPE_SLIDER:
                if( ( m.pktCmd == DCC::EN_SLIDER_BAR_LED || m.pktCmd == DCC::EN_SLIDER_LEVEL_LED ) && !d.empty() )
                    slider.setLedLevel( d[0] );
                break;

            default: break;
        }
    }

    void SimulatorBoard::setButtonLed( int id, bool on )
    {
        if( id >= DCC::EN_BANK_A && id <= DCC::EN_BANK_H )
        {
            if( !on )
                return;
            // Bank LEDs are exclusive; E-H share the A-D buttons.
            auto banks = mainControl.getBankButtons();
            for( int i = 0; i < 4; ++i )
                banks[(size_t)i]->setLed( i == id % 4 );
            pads.setBankName( juce::String::charToString( (juce::juce_wchar)( 'A' + id ) ) );
            return;
        }

        if( on && ( id == DCC::EN_PLAY || id == DCC::EN_STOP || id == DCC::EN_PLAYSTART ) )
        {
            playBtn.setLed( id == DCC::EN_PLAY );
            stopBtn.setLed( id == DCC::EN_STOP );
            playStartBtn.setLed( id == DCC::EN_PLAYSTART );
            return;
        }

        if( on && ( id == DCC::EN_FULL_LEVEL || id == DCC::EN_HALF_LEVEL ) )
        {
            fullLevelBtn.setLed( true );
            return;
        }

        auto range = ledMap.equal_range( id );
        for( auto it = range.first; it != range.second; ++it )
            it->second->setLed( on );
    }

    void SimulatorBoard::paint( juce::Graphics& g )
    {
        g.fillAll( palette::body );

        // Brand
        g.setColour( palette::text );
        g.setFont( juce::Font( 15.0f, juce::Font::bold ) );
        g.drawText( "KLARK TEKNIK", juce::Rectangle<int>( 24, 14, 300, 20 ), juce::Justification::centredLeft );
        g.setFont( juce::Font( 30.0f, juce::Font::bold ) );
        g.drawText( "KICK", juce::Rectangle<int>( 24, 32, 200, 32 ), juce::Justification::centredLeft );
        g.setColour( palette::text.withAlpha( 0.5f ) );
        g.setFont( juce::Font( 12.0f ) );
        g.drawText( "KickEM simulator - controls send front-panel MCU packets", juce::Rectangle<int>( 230, 38, 370, 24 ), juce::Justification::centredRight );

        // Wells
        g.setColour( palette::well );
        g.fillRoundedRectangle( juce::Rectangle<int>( 20, 70, 580, 310 ).toFloat(), 8.0f );
        g.fillRoundedRectangle( juce::Rectangle<int>( 1656, 14, 132, 556 ).toFloat(), 8.0f );
        g.fillRoundedRectangle( juce::Rectangle<int>( 1656, 580, 132, 360 ).toFloat(), 8.0f );

        // Screen bezel
        g.setColour( juce::Colours::black );
        g.fillRoundedRectangle( screenRect.expanded( 8 ).toFloat(), 6.0f );

        g.setColour( palette::text );
        g.setFont( juce::Font( 13.0f, juce::Font::bold ) );
        g.drawText( "Q-LINK", juce::Rectangle<int>( 1656, 16, 132, 18 ), juce::Justification::centred );
        g.drawText( "TRANSPORT", juce::Rectangle<int>( 1656, 582, 132, 18 ), juce::Justification::centred );
    }

    void SimulatorBoard::resized()
    {
        screen.setBounds( screenRect );

        slider.setBounds( 30, 78, 80, 296 );
        masterKnob.setBounds( 120, 80, 150, 140 );
        recKnob.setBounds( 120, 230, 150, 140 );
        fullLevelBtn.setBounds( 300, 90, 280, 80 );
        levels16Btn.setBounds( 300, 185, 280, 80 );
        eraseBtn.setBounds( 300, 280, 280, 80 );
        pads.setBounds( 20, 390, 580, 550 );

        mainControl.setBounds( 620, 640, 1024, 300 );

        for( int i = 0; i < 4; ++i )
            qlinks[(size_t)i]->setBounds( 1664, 36 + i * 118, 116, 112 );
        linkBtn.setBounds( 1666, 512, 112, 50 );

        int y = 604;
        for( auto* b : { &recBtn, &overdubBtn, &stopBtn, &playBtn, &playStartBtn } )
        {
            b->setBounds( 1666, y, 112, 58 );
            y += 66;
        }
    }

    //==============================================================================
    // Simulator (scaling wrapper)
    //==============================================================================
    Simulator::Simulator()
    {
        addAndMakeVisible( board );
        setSize( SimulatorBoard::boardWidth, SimulatorBoard::boardHeight );
    }

    void Simulator::paint( juce::Graphics& g )
    {
        g.fillAll( juce::Colour( 0xff202020 ) );
    }

    void Simulator::resized()
    {
        const float bw    = (float)SimulatorBoard::boardWidth;
        const float bh    = (float)SimulatorBoard::boardHeight;
        const float scale = juce::jmax( 0.1f, juce::jmin( getWidth() / bw, getHeight() / bh ) );

        board.setBounds( 0, 0, SimulatorBoard::boardWidth, SimulatorBoard::boardHeight );
        board.setTransform( juce::AffineTransform::scale( scale ).translated( ( getWidth() - bw * scale ) * 0.5f, ( getHeight() - bh * scale ) * 0.5f ) );
    }
}
