#include <cvrKernel/CalVR.h>

#include <cvrConfig/ConfigManager.h>
#include <cvrInput/TrackingManager.h>
#include <cvrMenu/MenuManager.h>
#include <cvrCollaborative/CollaborativeManager.h>
#include <cvrKernel/ScreenConfig.h>
#include <cvrKernel/ComController.h>
#include <cvrKernel/CVRViewer.h>
#include <cvrKernel/SceneManager.h>
#include <cvrKernel/FileHandler.h>
#include <cvrKernel/PluginManager.h>
#include <cvrKernel/InteractionManager.h>
#include <cvrKernel/Navigation.h>
#include <cvrKernel/ThreadedLoader.h>

#include <osgViewer/ViewerEventHandlers>

#include <iostream>
#include <vector>
#include <cstring>

#ifdef WIN32
#include <Winsock2.h>
#include <stdlib.h>
#pragma comment(lib, "wsock32.lib")
#endif

using namespace cvr;

CalVR * CalVR::_myPtr = NULL;

CalVR::CalVR()
{
    _config = NULL;
    _communication = NULL;
    _tracking = NULL;
    _interaction = NULL;
    _navigation = NULL;
    _viewer = NULL;
    _screens = NULL;
    _scene = NULL;
    _collaborative = NULL;
    _menu = NULL;
    _file = NULL;
    _plugins = NULL;
    _threadedLoader = NULL;
    _myPtr = this;
}

CalVR::~CalVR()
{
    if(_plugins)
    {
        delete _plugins;
    }
    if(_file)
    {
        delete _file;
    }
    if(_menu)
    {
        delete _menu;
    }
    if(_threadedLoader)
    {
        delete _threadedLoader;
    }
    if(_collaborative)
    {
        delete _collaborative;
    }
    if(_scene)
    {
        delete _scene;
    }
    if(_screens)
    {
        delete _screens;
    }
    if(_viewer)
    {
        delete _viewer;
    }
    if(_navigation)
    {
        delete _navigation;
    }
    if(_interaction)
    {
        delete _interaction;
    }
    if(_tracking)
    {
        delete _tracking;
    }
    if(_communication)
    {
        delete _communication;
    }
    if(_config)
    {
        delete _config;
    }
}

CalVR * CalVR::instance()
{
    return _myPtr;
}

bool CalVR::init(osg::ArgumentParser & args, std::string home)
{
    _home = home;
    
    
    if(!args.read("--host-name",_hostName))
    {
	char * chostname = getenv("CALVR_HOST_NAME");
	if(chostname)
	{
	    _hostName = chostname;
	}
	else
	{
	    char hostname[512];
	    gethostname(hostname,511);
	    _hostName = hostname;
	}
    }

    std::cerr << "HostName: " << _hostName << std::endl;

    _config = new cvr::ConfigManager();
    if(!_config->init())
    {
        std::cerr << "Error loading config file." << std::endl;
        return false;
    }

    _communication = cvr::ComController::instance();
    if(!_communication->init(&args))
    {
        std::cerr << "Error starting Communication Controller." << std::endl;
        return false;
    }

    // distribute files listed on command line
    std::vector<std::string> fileList;
    int files, size;
    if(_communication->isMaster())
    {
        for(int i = 1; i < args.argc(); i++)
        {
            fileList.push_back(args.argv()[i]);
        }
        files = fileList.size();
        _communication->sendSlaves(&files,sizeof(int));
        for(int i = 0; i < files; i++)
        {
            size = fileList[i].length() + 1;
            _communication->sendSlaves(&size,sizeof(int));
            _communication->sendSlaves((void*)fileList[i].c_str(),size);
        }
    }
    else
    {
        _communication->readMaster(&files,sizeof(int));
        char * temp;
        for(int i = 0; i < files; i++)
        {
            _communication->readMaster(&size,sizeof(int));
            temp = new char[size];
            _communication->readMaster(temp,size);
            fileList.push_back(temp);
            delete[] temp;
        }
    }

    _tracking = cvr::TrackingManager::instance();
    _tracking->init();

    _interaction = cvr::InteractionManager::instance();
    _interaction->init();

    _navigation = cvr::Navigation::instance();
    _navigation->init();

    // construct the viewer.
    _viewer = new cvr::CVRViewer();

    _screens = cvr::ScreenConfig::instance();
    if(!_screens->init())
    {
        std::cerr << "Error setting up screens." << std::endl;
        return false;
    }

    _screens->syncMasterScreens();

    _scene = cvr::SceneManager::instance();
    if(!_scene->init())
    {
        std::cerr << "Error setting up scene." << std::endl;
        return false;
    }

    _scene->setViewerScene(_viewer);
    //TODO: set this value based on threading model and window pipe mapping
    _viewer->setReleaseContextAtEndOfFrameHint(false);
    //_viewer.setReleaseContextAtEndOfFrameHint(true);

    _collaborative = cvr::CollaborativeManager::instance();
    _collaborative->init();

    _threadedLoader = cvr::ThreadedLoader::instance();

    osgViewer::StatsHandler * stats = new osgViewer::StatsHandler;
    stats->setKeyEventTogglesOnScreenStats((int)'S');
    stats->setKeyEventPrintsOutStats((int)'P');
    _viewer->addEventHandler(stats);

    _menu = cvr::MenuManager::instance();
    if(!_menu->init())
    {
        std::cerr << "Error setting up menu systems." << std::endl;
        return false;
    }

    _file = cvr::FileHandler::instance();

    _plugins = cvr::PluginManager::instance();
    _plugins->init();

    for(int i = 0; i < fileList.size(); i++)
    {
        cvr::FileHandler::instance()->loadFile(fileList[i]);
    }

    return true;
}

void CalVR::run()
{
    if(!_viewer->isRealized())
    {
        _viewer->realize();
    }

    int frameNum = 0;

    while(!_viewer->done())
    {
        //std::cerr << "Frame " << frameNum << std::endl;
        _viewer->frameStart();
        _viewer->advance(USE_REFERENCE_TIME);
        _viewer->eventTraversal();
        _tracking->update();
        _scene->update();
        _menu->update();
        _interaction->update();
        _navigation->update();
        _scene->postEventUpdate();
        _screens->computeViewProj();
        _screens->updateCamera();
        _collaborative->update();
        _threadedLoader->update();
        _plugins->preFrame();
        _viewer->updateTraversal();
        _viewer->renderingTraversals();

        if(_communication->getIsSyncError())
        {
            std::cerr << "Sync Error Exit." << std::endl;
            break;
        }

        _plugins->postFrame();

        frameNum++;
    }
}
