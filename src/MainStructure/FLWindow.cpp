//
//  FLWindow.cpp
//
//  Created by Sarah Denoux on 12/04/13.
//  Copyright (c) 2013 __MyCompanyName__. All rights reserved.
//

#include "FLWindow.h"

#include "faust/gui/faustqt.h"
#ifndef _WIN32
#include "faust/gui/OSCUI.h"
#include "faust/gui/httpdUI.h"
#endif


list<GUI*>               GUI::fGuiList;

#include <sstream>
#include "FLToolBar.h"
#include "utilities.h"
#include "FLSettings.h"

#ifdef REMOTE
#include "faust/remote-dsp.h"
#endif

#include "faust/llvm-dsp.h"

/****************************FaustLiveWindow IMPLEMENTATION***************************/

//------------CONSTRUCTION WINDOW
//@param : baseName = Window name
//@param : index = Index of the window
//@param : effect = effect that will be contained in the window
//@param : x,y = position on screen
//@param : home = current Session folder
//@param : osc/httpd port = port on which remote interface will be built 
//@param : machineName = in case of remote processing, the name of remote machine
FLWindow::FLWindow(QString& baseName, int index, FLEffect* eff, int x, int y, QString& home, int oscPort, int httpdport, const QString& machineName, const QString& ipMachine){
    
//  Enable Drag & Drop on window
    setAcceptDrops(true);
    
//  Creating Window Name
    fWindowIndex = index;
    fWindowName = baseName + "-" +  QString::number(fWindowIndex);
    
//    Initializing class members
    fEffect = eff;
    
#ifndef _WIN32
    fHttpdWindow = NULL;
    fHttpInterface = NULL;
    fOscInterface = NULL;
#endif
    fRCInterface = NULL;

    fMenu = NULL;
    
    fPortHttp = httpdport;
    fInterfaceUrl = "";
    fPortOsc = oscPort;
    
    fIPToHostName = new map<QString, std::pair<QString, int> >;
    
    fXPos = x;
    fYPos = y;
    
//    Creating Window Folder
    fHome = home + "/" + fWindowName;
    
    QDir direct;
    direct.mkdir(fHome);
    
//    Creating Audio Manager
    AudioCreator* creator = AudioCreator::_Instance(NULL);
    
    fAudioManager = creator->createAudioManager(FLWindow::audioShutDown, this);
    fClientOpen = false;
    
//    Not Sure It Is UseFull
//    setMinimumHeight(QApplication::desktop()->geometry().size().height()/4);
    
//    Set Menu & ToolBar
    fLastMigration = QDateTime::currentDateTime();
    setToolBar(machineName, ipMachine);
    set_MenuBar();
}

FLWindow::~FLWindow(){

    printf("FLWindow::~FLWindow %s \n", fWindowName.toStdString().c_str());
}

//------------------------WINDOW ACTIONS

//Show Window on front end with standard size
void FLWindow::frontShow(){
    
    setGeometry(fXPos, fYPos, 0, 0);
    adjustSize();
    
    show();
    raise();
    
    setMinimumSize(QSize(0, 0));
    setMaximumSize(QSize(QApplication::desktop()->geometry().size().width(), QApplication::desktop()->geometry().size().height()));
}

QString FLWindow::getErrorFromCode(int code){
 
#ifdef REMOTE
    if(code == ERROR_FACTORY_NOTFOUND){
        return "Impossible to create remote factory";
    }
    
    if(code == ERROR_INSTANCE_NOTCREATED){
        return "Impossible to create DSP Instance";
    }
    else if(code == ERROR_NETJACK_NOTSTARTED){
        return "NetJack Master not started";
    }
    else if (code == ERROR_CURL_CONNECTION){
        return "Curl connection failed";
    }
#endif
    
    return "ERROR not recognized";
}

//Initialization of User Interface + StartUp of Audio Client
//@param : init = if the window created is a default window.
//@param : error = in case init fails, the error is filled
bool FLWindow::init_Window(int init, QString& errorMsg){

    if(fEffect->isLocal()){

        if(!init_audioClient(errorMsg))
			return false;

        fCurrent_DSP = createDSPInstance(fEffect->getFactory());

    }
#ifdef REMOTE
    else{
        
        if(!init_audioClient(errorMsg, fEffect->getRemoteFactory()->numInputs(), fEffect->getRemoteFactory()->numOutputs()))
            return false;
        
        // Sending local IP for NetJack Connection
        int argc = 6;
        const char* argv[6];
        
        argv[0] = "--NJ_ip";
        string localString = searchLocalIP().toStdString();
        argv[1] = localString.c_str();
        argv[2] = "--NJ_latency";
        argv[3] = "10";
        argv[4] = "--NJ_compression";
        argv[5] = "64";
        
        int error;
        
        fCurrent_DSP = createRemoteDSPInstance(fEffect->getRemoteFactory(), argc, argv, fAudioManager->get_sample_rate(), fAudioManager->get_buffer_size(), RemoteDSPErrorCallback, this, error);
        
//        IN CASE FACTORY WAS LOST ON THE SERVER'S SIDE, IT IS RECOMPILED
//        NORMALLY NOT IMPORTANT FOR INIT (because a window is init in remote only when duplicated = already in current session = factory existant)
        if(fCurrent_DSP == NULL){
            
            if(error == ERROR_FACTORY_NOTFOUND){
                fEffect->reset();
                
                if(fEffect->reinit(errorMsg)){
                    fCurrent_DSP = createRemoteDSPInstance(fEffect->getRemoteFactory(), argc, argv, fAudioManager->get_sample_rate(), fAudioManager->get_buffer_size(),  RemoteDSPErrorCallback, this, error);
                }
            }
            errorMsg = getErrorFromCode(error);
        }
    }
#endif
    
    if (fCurrent_DSP == NULL){
        if(fEffect->isLocal())
            errorMsg = "Impossible to create a DSP instance"; 
        return false;
    }
    
    if(buildInterfaces(fCurrent_DSP, fEffect->getName())){

        if(init != kNoInit)
			print_initWindow(init);

        if(setDSP(errorMsg)){
            
            start_Audio();
            frontShow();
            
#ifdef _WIN32  
            if(fOscInterface){
                fOscInterface->run();
                fPortOsc = fOscInterface->getUDPPort();
            }
            if(fHttpInterface){
                
                fHttpInterface->run();
                
                fPortHttp = fHttpInterface->getTCPPort();
            }
                setWindowsOptions();
#endif
            fInterface->run();
            fInterface->installEventFilter(this);
                    
            return true;
        } 
        else
            deleteInterfaces();
    }
    else
        errorMsg = "Interface could not be allocated";
    
    return false;
}

void FLWindow::pressEvent()
{
    QDrag* reverseDrag = new QDrag(this);
    QMimeData* mimeData = new QMimeData;
    reverseDrag->setMimeData(mimeData);
    
    QPixmap fileIcon;
    fileIcon.load(":/Images/FileIcon.png");
    
    reverseDrag->setPixmap(fileIcon);
    
    if(QApplication::keyboardModifiers() == Qt::AltModifier){
        mimeData->setText(pathToContent(fEffect->getSource()));
    }
    else{
        QList<QUrl> listUrls;
        QUrl newURL(fEffect->getSource());
        listUrls.push_back(newURL);
        mimeData->setUrls(listUrls);
    }
    
    if (reverseDrag->exec(Qt::CopyAction) == Qt::CopyAction){}

}

bool FLWindow::eventFilter( QObject *obj, QEvent *ev ){
    
    if (ev->type() == QEvent::MouseMove && QApplication::mouseButtons()==Qt::LeftButton){
        
        pressEvent();
        return true;
    }
    else
        return QMainWindow::eventFilter(obj, ev);
}

//Modification of the process in the window
//@param : effect = effect that reemplaces the current one
//@param : error = in case update fails, the error is filled
bool FLWindow::update_Window(FLEffect* newEffect, QString& error){
    
    //Save the parameters of the actual interface
    fXPos = this->geometry().x();
    fYPos = this->geometry().y();
    
    save_Window(); 
    hide();
    
    //creating the new DSP instance
    dsp* charging_DSP = NULL;
    
    if(newEffect->isLocal())
        charging_DSP = createDSPInstance(newEffect->getFactory());
#ifdef REMOTE
    else{
        int argc = 6;
        const char* argv[6];
        
        argv[0] = "--NJ_ip";
        string localString = searchLocalIP().toStdString();
        argv[1] = localString.c_str();
        argv[2] = "--NJ_latency";
        argv[3] = "10";
        argv[4] = "--NJ_compression";
        argv[5] = "64";
        
        int errorMsg;
        
        charging_DSP = createRemoteDSPInstance(newEffect->getRemoteFactory(), argc, argv, fAudioManager->get_sample_rate(), fAudioManager->get_buffer_size(),  RemoteDSPErrorCallback, this, errorMsg);

//        IN CASE FACTORY WAS LOST ON THE SERVER'S SIDE, IT IS RECOMPILED
        if(charging_DSP == NULL){
            
            if(errorMsg == ERROR_FACTORY_NOTFOUND){
                newEffect->reset();
                
                if(newEffect->reinit(error)){
                    charging_DSP = createRemoteDSPInstance(newEffect->getRemoteFactory(), argc, argv, fAudioManager->get_sample_rate(), fAudioManager->get_buffer_size(),  RemoteDSPErrorCallback, this, errorMsg);
                }
            }
            error = getErrorFromCode(errorMsg);
        }
    }
#endif
    
    bool isUpdateSucessfull = false;
    
    if(charging_DSP){
        
        QString newName = newEffect->getName();
        
        if(fAudioManager->init_FadeAudio(error, newName.toStdString().c_str(), charging_DSP)){
            
            deleteInterfaces();
            
            //Set the new interface & Recall the parameters of the window
            if(buildInterfaces(charging_DSP, newName)){
                
                recall_Window();
                
                //Start crossfade and wait for its end
                fAudioManager->start_Fade();
                
                setGeometry(fXPos, fYPos, 0, 0);
                adjustSize();
                show();
                
                fAudioManager->wait_EndFade();
                
                //SWITCH the current DSP as the dropped one
                dsp* VecInt;
                
                VecInt = fCurrent_DSP;
                fCurrent_DSP = charging_DSP; 
                charging_DSP = VecInt;
                
                FLEffect* effectInter = fEffect;
                fEffect = newEffect;
                newEffect = effectInter;
                
                isUpdateSucessfull = true;
            }
            else{
                buildInterfaces(fCurrent_DSP, fEffect->getName());
                recall_Window();

                error = "Impossible to allocate new interface";
            }
            
            //Step 12 : Launch User Interface
            fInterface->run();
            fInterface->installEventFilter(this);
#ifdef _WIN32
            if(fOscInterface){   
                fOscInterface->run();
                fPortOsc = fOscInterface->getUDPPort();
            }
            if(fHttpInterface){
                fHttpInterface->run();
                fPortHttp = fHttpInterface->getTCPPort();
            }
            
            setWindowsOptions();
#endif
        }

//-----Delete Charging DSP if update fails || Delete old DSP if update suceeds        
        
        if(newEffect->isLocal())
            deleteDSPInstance((llvm_dsp*)charging_DSP);
#ifdef REMOTE  
        else
            deleteRemoteDSPInstance((remote_dsp*)charging_DSP);
#endif
    }
    else{
        if(newEffect->isLocal())
            error = "Impossible to allocate DSP";
    }
    
    show();
    
    return isUpdateSucessfull;
}

//------------TOOLBAR RELATED ACTIONS

//Set up of the Window ToolBar
void FLWindow::setToolBar(const QString& machineName, const QString& ipMachine){
    
    fMenu = new FLToolBar(this);
    
    addToolBar(fMenu);
     
    connect(fMenu, SIGNAL(modified(QString, int, int, int)), this, SLOT(modifiedOptions(const QString&, int, int, int)));
    connect(fMenu, SIGNAL(sizeGrowth()), this, SLOT(resizingBig()));
    connect(fMenu, SIGNAL(sizeReduction()), this, SLOT(resizingSmall()));
    connect(fMenu, SIGNAL(switchMachine(const QString&, int)), this, SLOT(redirectSwitch(const QString&, int)));
    connect(fMenu, SIGNAL(switch_http(bool)), this, SLOT(switchHttp(bool)));
    connect(fMenu, SIGNAL(switch_osc(bool)), this, SLOT(switchOsc(bool)));
    
#ifdef REMOTE
    fMenu->setRemoteButtonName(machineName, ipMachine);
#endif
    
}

//Set the windows options with current values
void FLWindow::setWindowsOptions(){
    
    QString textOptions = fEffect->getCompilationOptions();
    if(textOptions.compare(" ") == 0)
        textOptions = "";
    
    fMenu->setOptions(textOptions);
    fMenu->setVal(fEffect->getOptValue());
    fMenu->setPort(fPortHttp);
    fMenu->setPortOsc(fPortOsc);
}

//Reaction to the modifications of the ToolBar options
void FLWindow::modifiedOptions(QString text, int value, int port, int portOsc){
    
    if(fPortHttp != port){
        fPortHttp = port;
        
        save_Window();
#ifndef _WIN32
        delete fHttpInterface;
        fHttpInterface = NULL;
        
        allocateHttpInterface();
        
        fCurrent_DSP->buildUserInterface(fHttpInterface);
        recall_Window();
        fHttpInterface->run();
        
        fPortHttp = fHttpInterface->getTCPPort();
        setWindowsOptions();
#endif
        
    }
    
    if(fPortOsc != portOsc){
        fPortOsc = portOsc;
        
        save_Window();
        
#ifndef _WIN32
        delete fOscInterface;
        fOscInterface = NULL;
        
        allocateOscInterface();
        
        fCurrent_DSP->buildUserInterface(fOscInterface);
        recall_Window();
        fOscInterface->run();
        
        fPortOsc = fOscInterface->getUDPPort();
        setWindowsOptions();
#endif
    }
    
    fEffect->update_compilationOptions(text, value);
}

//Reaction to the resizing the toolbar
void FLWindow::resizingSmall(){
    
    setMinimumSize(QSize(0,0));
    adjustSize();
}

void FLWindow::resizingBig(){
    
    //    QSize winSize = fMenu->geometry().size();
    //    winSize += fMenu->minimumSize();
    //   
    //    
    QSize winMinSize = minimumSize();
    winMinSize += fMenu->geometry().size();
    
    //    setGeometry(0,0,winSize.width(), winSize.height());
    setMinimumSize(winMinSize);
    //
    adjustSize();
}

//Redirection machine switch
void FLWindow::redirectSwitch(const QString& ip, int port){
    emit migrate(ip, port);
}

//Redirecting result of migration to toolbar 
void FLWindow::migrationFailed(){
    fMenu->remoteFailed();
}

void FLWindow::migrationSuccessfull(){
    fMenu->remoteSuccessfull();
}

//Accessor to processing machine name
QString FLWindow::get_machineName(){
    return fMenu->machineName();
}

//Accessor to processing machine ip
QString FLWindow::get_ipMachine(){
    return fMenu->ipServer();
}

//Accessor to Http & Osc Port
int FLWindow::get_Port(){
    
#ifndef _WIN32
    if(fHttpInterface != NULL)
        return fHttpInterface->getTCPPort();
    else
#endif
// If the interface is not enabled, it's not running on any port
        return 0;
}

int FLWindow::get_oscPort(){
    
    return fPortOsc;
}

//------------ALLOCATION/DESALLOCATION OF INTERFACES

void FLWindow::disableOSCInterface(){
    fMenu->switchOsc(false);
}

void FLWindow::switchOsc(bool on){
 
    if(on){
        
        save_Window();
#ifndef _WIN32
        allocateOscInterface();
        fCurrent_DSP->buildUserInterface(fOscInterface);
        recall_Window();
        fOscInterface->run();
        fPortOsc = fOscInterface->getUDPPort();
        setWindowsOptions();
#endif
    }
    else{
        delete fOscInterface;
        fOscInterface = NULL;
    }
}

void catch_OSCError(void* arg){
    
    FLWindow* win = (FLWindow*)(arg);
    
    win->errorPrint("Too many OSC interfaces opened at the same time! Connection could not start");
    win->disableOSCInterface();
}

//Allocation of Interfaces
void FLWindow::allocateOscInterface(){
    
    if(fOscInterface == NULL){
        
        char* argv[3];
        argv[0] = (char*)(fWindowName.toStdString().c_str());
        argv[1] = "-port";
        
        argv[2] = (char*) (QString::number(fPortOsc).toLatin1().data());
        
#ifndef WIN32
        fOscInterface = new OSCUI(argv[0], 3, argv, NULL, &catch_OSCError, this);
#endif
    }
}

//Building QT Interface | Osc Interface | Parameter saving Interface | ToolBar
bool FLWindow::buildInterfaces(dsp* dsp, const QString& nameEffect){
    
    //Set parameters in ToolBar
    setWindowsOptions();
    
    fRCInterface = new FUI;
    
#ifndef _WIN32
    if(fMenu->isOscOn())
        allocateOscInterface();
    
    if(fMenu->isHttpOn())
        allocateHttpInterface();
#endif
    
    if(fRCInterface){
        
        //Window tittle is build with the window Name + effect Name
        QString intermediate = fWindowName + " : " + nameEffect;
        
        fInterface = new QTGUI(this, intermediate.toStdString().c_str());
        
        if(fInterface){
            
            dsp->buildUserInterface(fRCInterface);
            dsp->buildUserInterface(fInterface);
            
#ifndef _WIN32
            if(fOscInterface)
                dsp->buildUserInterface(fOscInterface);
            
            if(fHttpInterface)
                dsp->buildUserInterface(fHttpInterface);
#endif
            return true;
        }
    }  
    return false;
}

//Delete of QTinterface and of saving graphical interface
void FLWindow::deleteInterfaces(){
    
    delete fInterface;
    delete fRCInterface;
#ifndef _WIN32
    if(fMenu->isOscOn()){
        delete fOscInterface;
        fOscInterface = NULL;
    }
    if(fMenu->isHttpOn()){
        delete fHttpInterface;
        fHttpInterface = NULL;
    }
#endif
    fInterface = NULL;
    fRCInterface = NULL;
}

//------------DEFAULT WINDOW FUNCTIONS

//Does window contain a default Faust process?
bool FLWindow::is_Default(){
    
    QString sourceContent = pathToContent(fEffect->getSource());
    
    if(sourceContent.compare("process = !,!:0,0;") == 0)
        return true;
    else 
        return false;
}

//Artificial content of a default window
void FLWindow::print_initWindow(int typeInit){
    
    QPixmap dropImage;
    
    if(typeInit == kInitBlue)
        dropImage.load(":/Images/DropYourFaustLife_Blue.png");
    else if(typeInit == kInitWhite)
        dropImage.load(":/Images/DropYourFaustLife_White.png");
        
    dropImage.scaledToHeight(10, Qt::SmoothTransformation);

    QLabel *image = new QLabel();
//    image->setMinimumSize (dropImage.width()*3, dropImage.height()*3);
    
    image->installEventFilter(this);
    
    image->setPixmap(dropImage);
    image->setAlignment(Qt::AlignCenter);
    setCentralWidget(image);
}

//------------------------CLOSING ACTIONS

//Reaction to click an x button
void FLWindow::closeEvent(QCloseEvent* event){
    
    if(QApplication::keyboardModifiers() == Qt::AltModifier)
        emit shut_AllWindows();
    else
        emit closeWin();
}

//During the execution, when a window is shut, its associate folder has to be removed
void FLWindow::shut_Window(){

    deleteDirectoryAndContent(fHome);    
    close_Window();
}

//Closing the window without removing its property for example when the application is quit
void FLWindow::close_Window(){
    
    hide();
    
    if(fClientOpen && fAudioManager)
        fAudioManager->stop();
    
    deleteInterfaces();
    
#ifndef _WIN32
    if(fHttpdWindow){
        fHttpdWindow->deleteLater();
        //fHttpdWindow = NULL;
    }
#endif
    
    if(fEffect->isLocal())
        deleteDSPInstance((llvm_dsp*)fCurrent_DSP);
#ifdef REMOTE
    else
        deleteRemoteDSPInstance((remote_dsp*)fCurrent_DSP);
#endif
    
    if(fAudioManager)
        delete fAudioManager;
    
    if(fMenu)
        delete fMenu;
    
    blockSignals(true);
}

//------------------------DRAG AND DROP ACTIONS

//Reaction to drop on the window
void FLWindow::dropEvent ( QDropEvent * event ){
    
    //The widget was hidden from crossing of an object through the window
    this->centralWidget()->show();
    
    QList<QString>    sourceList;    
    
	int numberCharToErase = 0;
#ifndef _WIN32
	numberCharToErase = 8;
#else
	numberCharToErase = 7;
#endif
	
    if (event->mimeData()->hasUrls()) {

        QList<QString>    sourceList;
        QList<QUrl> urls = event->mimeData()->urls();
        QList<QUrl>::iterator i;
        
        for (i = urls.begin(); i != urls.end(); i++) {
            
            if(i->isLocalFile() || QFileInfo(i->toString()).exists()){
                
                QString fileName;
                
                if(i->isLocalFile())
                    fileName = i->toLocalFile();
                else
                    fileName =  i->toString();
                               
                sourceList.push_back(fileName);
                
                event->accept();
            }
            else{
            	QString dsp = event->mimeData()->text();
            	sourceList.push_back(dsp);
            }
        }   
        emit drop(sourceList);
    }
        //The event is not entirely handled by the window, it is redirected to the application through the drop signal
    else if (event->mimeData()->hasText()){
        
        event->accept();
        
        QString TextContent = event->mimeData()->text();

        /*if(event->mimeData()->text().indexOf("file://") == 0){
        	TextContent = QString(event->mimeData()->text()).right(event->mimeData()->text().size()-numberCharToErase);
       	}
        else*/
        
        sourceList.push_back(TextContent);
                
        emit drop(sourceList);
    }
}

//That way the drag movement is more visible : the central widget is hidden when an object is crossing the window and reset visible when the object leaves the window
void FLWindow::dragEnterEvent ( QDragEnterEvent * event ){
    
    if (event->mimeData()->hasFormat("text/uri-list") || event->mimeData()->hasFormat("text/plain")){
        
        if (event->mimeData()->hasUrls()) {
            QList<QString>    sourceList;
            QList<QUrl> urls = event->mimeData()->urls();
            QList<QUrl>::iterator i;
            
            for (i = urls.begin(); i != urls.end(); i++) {
                
                    if(QFileInfo(i->toString()).completeSuffix().compare("dsp") == 0 || QFileInfo(i->toString()).completeSuffix().compare("wav") == 0){
                        
                        centralWidget()->hide();
                        event->acceptProposedAction();   
                    }
            }
            
        }
        else if(event->mimeData()->hasFormat("text/plain")){
            centralWidget()->hide();
            event->acceptProposedAction();      
        }
    }
}

void FLWindow::dragLeaveEvent ( QDragLeaveEvent * /*event*/ ){
    //    setWindowFlags();
    centralWidget()->show();
}

//-------------------------AUDIO FUNCTIONS

//Start/Stop of audio
void FLWindow::stop_Audio(){
    
#ifdef REMOTE
    
    if(!fEffect->isLocal()){
        
        remote_dsp* currentDSP = (remote_dsp*) fCurrent_DSP;
        currentDSP->stopAudio();
    }
    
#endif
    
    fAudioManager->stop();
    fClientOpen = false;
}

void FLWindow::start_Audio(){
    
    recall_Window();
    
    fAudioManager->start();
    
    QString connectFile = fHome + "/" + fWindowName + ".jc";

    fAudioManager->connect_Audio(connectFile.toStdString());
    
    fClientOpen = true;
    
#ifdef REMOTE
    if(!fEffect->isLocal()){
        
        remote_dsp* currentDSP = (remote_dsp*) fCurrent_DSP;
        currentDSP->startAudio();
    }
#endif
}


//In case audio clients collapse, the architecture has to be changed
void FLWindow::audioShutDown(const char* msg, void* arg){

    ((FLWindow*)arg)->audioShutDown(msg);
}

void FLWindow::audioShutDown(const char* msg){
    AudioCreator* creator = AudioCreator::_Instance(NULL);
    
//    creator->change_Architecture();
    emit errorPrint(msg);
}

//Switch of Audio architecture
bool FLWindow::update_AudioArchitecture(QString& error){
    
    AudioCreator* creator = AudioCreator::_Instance(NULL);
    delete fAudioManager;
    
    fAudioManager = creator->createAudioManager();
    
    if(init_audioClient(error) && setDSP(error))
        return true;
    else
        return false;
}

//Initialization of audio Client
bool FLWindow::init_audioClient(QString& error){
    
	if(fAudioManager->initAudio(error, fWindowName.toStdString().c_str()))
        return true;
    else
        return false;

}

//Initialization of audio Client Reimplemented
bool FLWindow::init_audioClient(QString& error, int numInputs, int numOutputs){
    
	if(fAudioManager->initAudio(error, fWindowName.toStdString().c_str(), fEffect->getName().toStdString().c_str(), numInputs, numOutputs))
        return true;
    else
        return false;
    
}

bool FLWindow::setDSP(QString& error){
    
    if(fAudioManager->setDSP(error, fCurrent_DSP, fEffect->getName().toLatin1().data())){            
        recall_Window();
        return true;
    }
    else{
        return false;
    }
}

//------------------------SAVING WINDOW ACTIONS

//When the window is imported in a current session, its properties may have to be updated
void FLWindow::update_ConnectionFile(std::list<std::pair<std::string, std::string> > changeTable){
    
    QString connectFile = fHome + "/" + fWindowName + ".jc";
    fAudioManager->change_Connections(connectFile.toStdString(), changeTable);
}

//Read/Write window properties in saving file
void FLWindow::save_Window(){
    
    //Graphical parameters//
    QString rcfilename = fHome + "/" + fWindowName + ".rc";
    fRCInterface->saveState(rcfilename.toLatin1().data());
    
    //Audio Connections parameters
    QString connectFile = fHome + "/" + fWindowName + ".jc";
    
    fAudioManager->save_Connections(connectFile.toStdString());
}

void FLWindow::recall_Window(){
    
    //Graphical parameters//
    QString rcfilename = fHome + "/" + fWindowName + ".rc";
    QString toto(rcfilename);
    
    if(QFileInfo(toto).exists())
        fRCInterface->recallState(rcfilename.toStdString().c_str());
}

//------------------------ACCESSORS

QString FLWindow::get_nameWindow(){
    return fWindowName;
}

int FLWindow::get_indexWindow(){
    return fWindowIndex;
}

FLEffect* FLWindow::get_Effect(){
    return fEffect;
}

int FLWindow::get_x(){
    
    fXPos = this->geometry().x();
    return fXPos;
}

int FLWindow::get_y(){
    fYPos = this->geometry().y();
    return fYPos;
}

//------------------------HTTPD

//Calculation of screen position of the HTTP window, depending on its index
int FLWindow::calculate_Coef(){
    
    int multiplCoef = fWindowIndex;
    while(multiplCoef > 20){
        multiplCoef-=20;
    }
    return multiplCoef;
}

#ifndef _WIN32
void FLWindow::resetHttpInterface(){

    fMenu->switchHttp(false);
    fMenu->switchHttp(true);
}

void FLWindow::allocateHttpInterface(){

    if(fMenu->isHttpOn()){
        
        QString optionPort = "-port";
        QString windowTitle = fWindowName + ":" + fEffect->getName();
        fPortHttp = fMenu->getPort();
        
        char* argv[3];
        
        argv[0] = (char*)(windowTitle.toLatin1().data());
        argv[1] = (char*)(optionPort.toLatin1().data());
        argv[2] = (char*)(QString::number(fPortHttp).toStdString().c_str());
        
        fHttpInterface = new httpdUI(argv[0], 3, argv);
    }
}

void FLWindow::switchHttp(bool on){
    if(on){
        save_Window();
        allocateHttpInterface();
        
        fCurrent_DSP->buildUserInterface(fHttpInterface);
        recall_Window();
        fHttpInterface->run();
        
        fPortHttp = fHttpInterface->getTCPPort();
        setWindowsOptions();

    }
    else{
        delete fHttpInterface;
        fHttpInterface = NULL;
    } 
}

void FLWindow::viewQrCode(){

    if(fHttpInterface == NULL){
        allocateHttpInterface();
    }
    
    if(fHttpdWindow != NULL){
        delete fHttpdWindow;
        fHttpdWindow = NULL;
    }
    
    fHttpdWindow = new HTTPWindow();
    connect(fHttpdWindow, SIGNAL(toPNG()), this, SLOT(exportToPNG()));

    if(fHttpdWindow){
        
        QString fullUrl("");
        
        fInterfaceUrl = "http://";
        fInterfaceUrl += searchLocalIP();
        fInterfaceUrl += ":";
        
        fullUrl = fInterfaceUrl;
        
        fullUrl += QString::number(FLSettings::getInstance()->value("General/Network/HttpDropPort", 7777).toInt());
        fullUrl += "/";
        fInterfaceUrl += QString::number(fHttpInterface->getTCPPort());
        fullUrl += QString::number(fHttpInterface->getTCPPort());
        
        fInterface->displayQRCode(fullUrl, fHttpdWindow);
        fHttpdWindow->move(calculate_Coef()*10, 0);
        
        QString windowTitle = fWindowName + ":" + fEffect->getName();
        
        fHttpdWindow->setWindowTitle(windowTitle);
        fHttpdWindow->raise();
        fHttpdWindow->show();
        fHttpdWindow->adjustSize();
    }
    else
        emit error("Enable Http Before Asking for Qr Code");
}

void FLWindow::exportToPNG(){

    QFileDialog* fileDialog = new QFileDialog;
    fileDialog->setConfirmOverwrite(true);
    
    QString filename;
    
    filename = fileDialog->getSaveFileName(NULL, "PNG Name", tr(""), tr("(*.png)"));
    
    QString errorMsg;
    
    if(!fInterface->toPNG(filename, errorMsg))
        emit error(errorMsg.toStdString().c_str());
}

bool FLWindow::is_httpdWindow_active() {

    if(fHttpdWindow)
        return fHttpdWindow->isActiveWindow();
}

void FLWindow::hide_httpdWindow() {
    if(fHttpdWindow)
        fHttpdWindow->hide();
}

QString FLWindow::get_HttpUrl() {
    return fInterfaceUrl;
}
#endif

//------------------------Right Click Reaction

void FLWindow::contextMenuEvent(QContextMenuEvent* ev) {
    
    fWindowMenu->exec(ev->globalPos());
}

void FLWindow::set_MenuBar(){
    
    //----------------FILE
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    
    QAction* newAction = new QAction(tr("&New Default Window"), this);
    newAction->setShortcut(tr("Ctrl+N"));
    newAction->setToolTip(tr("Open a new empty file"));
    connect(newAction, SIGNAL(triggered()), this, SLOT(create_Empty()));
    
    QAction* openAction = new QAction(tr("&Open..."),this);
    openAction->setShortcut(tr("Ctrl+O"));
    openAction->setToolTip(tr("Open a DSP file"));
    connect(openAction, SIGNAL(triggered()), this, SLOT(open_New()));
    
    QMenu* menuOpen_Example = new QMenu(tr("&Open Example"), fileMenu);
    
    QDir examplesDir(":/");
    
    if(examplesDir.cd("Examples")){
        
        QFileInfoList children = examplesDir.entryInfoList(QDir::Files | QDir::Drives | QDir::NoDotAndDotDot);
        
        QFileInfoList::iterator it;
        int i = 0; 
        
        QAction** openExamples = new QAction* [children.size()];
        
        for(it = children.begin(); it != children.end(); it++){
            
            openExamples[i] = new QAction(it->baseName(), menuOpen_Example);
            openExamples[i]->setData(QVariant(it->absoluteFilePath()));
            connect(openExamples[i], SIGNAL(triggered()), this, SLOT(open_Example()));
            
            menuOpen_Example->addAction(openExamples[i]);
            i++;
        }
    }
    
    QMenu* openRecentAction = new QMenu(tr("&Open Recent File"), fileMenu);
    
    fRecentFileAction = new QAction* [kMAXRECENTFILES];
    
    for(int i=0; i<kMAXRECENTFILES; i++){
        fRecentFileAction[i] = new QAction(this);
        fRecentFileAction[i]->setVisible(false);
        connect(fRecentFileAction[i], SIGNAL(triggered()), this, SLOT(open_Recent_File()));
        
        openRecentAction->addAction(fRecentFileAction[i]);
    }
        
//SESSION
    
    QAction* takeSnapshotAction = new QAction(tr("&Take Snapshot"),this);
    takeSnapshotAction->setShortcut(tr("Ctrl+S"));
    takeSnapshotAction->setToolTip(tr("Save current state"));
    connect(takeSnapshotAction, SIGNAL(triggered()), this, SLOT(take_Snapshot()));
    
    QAction* recallSnapshotAction = new QAction(tr("&Recall Snapshot..."),this);
    recallSnapshotAction->setShortcut(tr("Ctrl+R"));
    recallSnapshotAction->setToolTip(tr("Close all the opened window and open your snapshot"));
    connect(recallSnapshotAction, SIGNAL(triggered()), this, SLOT(recallSnapshot()));
    
    QMenu* recallRecentAction = new QMenu(tr("&Recall Recent Snapshot"), fileMenu);
    QMenu* importRecentAction = new QMenu(tr("&Import Recent Snapshot"), fileMenu);
    
    fRrecentSessionAction = new QAction* [kMAXRECENTSESSIONS];
    fIrecentSessionAction = new QAction* [kMAXRECENTSESSIONS];
    
    for(int i=0; i<kMAXRECENTSESSIONS; i++){
        fRrecentSessionAction[i] = new QAction(this);
        fRrecentSessionAction[i]->setVisible(false);
        connect(fRrecentSessionAction[i], SIGNAL(triggered()), this, SLOT(recall_Recent_Session()));
        
        recallRecentAction->addAction(fRrecentSessionAction[i]);
        
        fIrecentSessionAction[i] = new QAction(this);
        fIrecentSessionAction[i]->setVisible(false);
        connect(fIrecentSessionAction[i], SIGNAL(triggered()), this, SLOT(import_Recent_Session()));
        
        importRecentAction->addAction(fIrecentSessionAction[i]);
    }
    
    QAction* importSnapshotAction = new QAction(tr("&Import Snapshot..."),this);
    importSnapshotAction->setShortcut(tr("Ctrl+I"));
    importSnapshotAction->setToolTip(tr("Import your snapshot in the current session"));
    connect(importSnapshotAction, SIGNAL(triggered()), this, SLOT(importSnapshot()));
    
    QAction* shutAction = new QAction(tr("&Close Window"),this);
    shutAction->setShortcut(tr("Ctrl+W"));
    shutAction->setToolTip(tr("Close the current Window"));
    connect(shutAction, SIGNAL(triggered()), this, SLOT(shut()));
    
    QAction* shutAllAction = new QAction(tr("&Close All Windows"),this);
    shutAllAction->setShortcut(tr("Ctrl+Alt+W"));
    shutAllAction->setToolTip(tr("Close all the Windows"));
    connect(shutAllAction, SIGNAL(triggered()), this, SLOT(shut_All()));
    
    QAction* closeAllAction = new QAction(tr("&Quit FaustLive"),this);
	closeAllAction->setShortcut(tr("Ctrl+Q"));
    closeAllAction->setToolTip(tr("Close the application"));   
    connect(closeAllAction, SIGNAL(triggered()), this, SLOT(closeAll()));
    
    fileMenu->addAction(newAction);    
    fileMenu->addSeparator();
    fileMenu->addAction(openAction);
    fileMenu->addAction(menuOpen_Example->menuAction());
    fileMenu->addAction(openRecentAction->menuAction());
    fileMenu->addSeparator();
    fileMenu->addAction(takeSnapshotAction);
    fileMenu->addSeparator();
    fileMenu->addAction(recallSnapshotAction);
    fileMenu->addAction(recallRecentAction->menuAction());
    fileMenu->addSeparator();
    fileMenu->addAction(importSnapshotAction);
    fileMenu->addAction(importRecentAction->menuAction());
    fileMenu->addSeparator();
    fileMenu->addAction(shutAction);
    fileMenu->addAction(shutAllAction);
    fileMenu->addSeparator();
    fileMenu->addAction(closeAllAction);
    
    menuBar()->addSeparator();

    //-----------------Window
    
    QAction* editAction = new QAction(tr("&Edit Faust Source"), this);
    editAction->setShortcut(tr("Ctrl+E"));
    editAction->setToolTip(tr("Edit the source"));
    connect(editAction, SIGNAL(triggered()), this, SLOT(edit()));
    
    QAction* pasteAction = new QAction(tr("&Paste"),this);
    pasteAction->setShortcut(tr("Ctrl+V"));
    pasteAction->setToolTip(tr("Paste a DSP"));
    connect(pasteAction, SIGNAL(triggered()), this, SLOT(paste()));
    
    QAction* duplicateAction = new QAction(tr("&Duplicate"),this);
    duplicateAction->setShortcut(tr("Ctrl+D"));
    duplicateAction->setToolTip(tr("Duplicate current DSP"));
    connect(duplicateAction, SIGNAL(triggered()), this, SLOT(duplicate()));
    
//#if-group:
#ifndef _WIN32
//#ifdef HTTPCTRL
    QAction* httpdViewAction = new QAction(tr("&View QRcode"),this);
    httpdViewAction->setShortcut(tr("Ctrl+K"));
    httpdViewAction->setToolTip(tr("Print the QRcode of TCP protocol"));
    connect(httpdViewAction, SIGNAL(triggered()), this, SLOT(httpd_View()));
#endif
    
    QAction* svgViewAction = new QAction(tr("&View SVG Diagram"),this);
    svgViewAction->setShortcut(tr("Ctrl+G"));
    svgViewAction->setToolTip(tr("Open the SVG Diagram in a browser"));
    connect(svgViewAction, SIGNAL(triggered()), this, SLOT(svg_View()));
    
    QAction* exportAction = new QAction(tr("&Export As..."), this);
    exportAction->setShortcut(tr("Ctrl+P"));
    exportAction->setToolTip(tr("Export the DSP in whatever architecture you choose"));
    connect(exportAction, SIGNAL(triggered()), this, SLOT(exportManage()));
    
    fWindowMenu = menuBar()->addMenu(tr("&Window"));
    fWindowMenu->addAction(editAction);
    fWindowMenu->addAction(pasteAction);
    fWindowMenu->addAction(duplicateAction);
    fWindowMenu->addSeparator();
#if !defined (_WIN32) /*|| defined HTTPCTRL*/    
    fWindowMenu->addAction(httpdViewAction);
#endif
    fWindowMenu->addAction(svgViewAction);
    fWindowMenu->addSeparator();
    fWindowMenu->addAction(exportAction);
    
    menuBar()->addSeparator();
    
    //-----------------NAVIGATE
    
    fNavigateMenu = new QMenu;
    fNavigateMenu = menuBar()->addMenu(tr("&Navigate"));    
    menuBar()->addSeparator();
    
    //---------------------MAIN MENU
    
    QAction* aboutQtAction = new QAction(tr("&About Qt"), this);
    aboutQtAction->setToolTip(tr("Show the library's About Box"));
    connect(aboutQtAction, SIGNAL(triggered()), this, SLOT(aboutQt()));
    
    QAction* preferencesAction = new QAction(tr("&Preferences"), this);
    preferencesAction->setToolTip(tr("Set the preferences of the application"));
    connect(preferencesAction, SIGNAL(triggered()), this, SLOT(preferences()));
    
    //--------------------HELP
    
    QAction* aboutAction = new QAction(tr("&Help..."), this);
    aboutAction->setToolTip(tr("Show the library's About Box"));
    connect(aboutAction, SIGNAL(triggered()), this, SLOT(aboutFaustLive()));

    
    QAction* presentationAction = new QAction(tr("&About FaustLive"), this);
    presentationAction->setToolTip(tr("Show the presentation Menu"));
    connect(presentationAction, SIGNAL(triggered()), this, SLOT(show_presentation()));
    
    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    
    helpMenu->addAction(aboutQtAction);
    helpMenu->addSeparator();
    helpMenu->addAction(aboutAction);
    helpMenu->addAction(presentationAction);
    helpMenu->addSeparator();
    helpMenu->addAction(preferencesAction);
}

//------SLOTS FROM MENU ACTIONS THAT ARE REDIRECTED
void FLWindow::create_Empty(){
    emit create_Empty_Window();
}

void FLWindow::open_New(){
    emit open_New_Window();
}

void FLWindow::open_Example(){
    
    QAction* action = qobject_cast<QAction*>(sender());
    QString toto(action->data().toString());
    
    emit open_Ex(toto);
}

void FLWindow::take_Snapshot(){
    emit takeSnapshot();
}

void FLWindow::recallSnapshot(){
    emit recallSnapshotFromMenu();
}

void FLWindow::importSnapshot(){
    emit importSnapshotFromMenu();
}

void FLWindow::shut(){
    emit close();
//    emit closeWin();
}

void FLWindow::shut_All(){
    emit shut_AllWindows();
}

void FLWindow::closeAll(){

    emit close_AllWindows();
}

void FLWindow::edit(){
    emit edit_Action();
}

void FLWindow::paste(){
    emit paste_Action();
}

void FLWindow::duplicate(){
    emit duplicate_Action();
}

#ifndef _WIN32
void FLWindow::httpd_View(){

    fMenu->switchHttp(true);    
    viewQrCode();
}
#endif

void FLWindow::svg_View(){
    emit svg_View_Action();
}

void FLWindow::exportManage(){
    emit export_Win();
}

void FLWindow::aboutQt(){
    emit show_aboutQt();
}

void FLWindow::preferences(){
    emit show_preferences();
}

void FLWindow::aboutFaustLive(){
    emit apropos();
}

void FLWindow::show_presentation(){
    emit show_presentation_Action();
}

void FLWindow::set_RecentFile(QList<std::pair<QString, QString> > recents){
    fRecentFiles = recents;
}

void FLWindow::update_RecentFileMenu(){
    int j = 0;
    
    QList<std::pair<QString, QString> >::iterator it;
    
    for (it = fRecentFiles.begin(); it != fRecentFiles.end(); it++) {
        
        if(j<kMAXRECENTFILES){
            
            QString text;
            text += it->second;
            fRecentFileAction[j]->setText(text);
            fRecentFileAction[j]->setData(it->first);
            fRecentFileAction[j]->setVisible(true);
            
            j++;
        }
    }
}

void FLWindow::open_Recent_File(){
    
    QAction* action = qobject_cast<QAction*>(sender());
    QString toto(action->data().toString());
    
    emit open_File(toto);
}

void FLWindow::set_RecentSession(QStringList recents){
    fRecentSessions = recents;
}

void FLWindow::update_RecentSessionMenu(){

    QMutableStringListIterator i(fRecentSessions);
    while(i.hasNext()){
        if(!QFile::exists(i.next()))
            i.remove();
    }
    
    for(int j=0; j<kMAXRECENTSESSIONS; j++){
        if(j<fRecentSessions.count()){
            
            QString path = QFileInfo(fRecentSessions[j]).baseName();
            
            QString text = tr("&%1 %2").arg(j+1).arg(path);
            fRrecentSessionAction[j]->setText(text);
            fRrecentSessionAction[j]->setData(fRecentSessions[j]);
            fRrecentSessionAction[j]->setVisible(true);
            
            fIrecentSessionAction[j]->setText(text);
            fIrecentSessionAction[j]->setData(fRecentSessions[j]);
            fIrecentSessionAction[j]->setVisible(true);
        }
        else{
            fRrecentSessionAction[j]->setVisible(false);
            fIrecentSessionAction[j]->setVisible(false);
        }
    }
}

void FLWindow::recall_Recent_Session(){

    QAction* action = qobject_cast<QAction*>(sender());
    QString toto(action->data().toString());
    
    emit recall_Snapshot(toto, false);
}

void FLWindow::import_Recent_Session(){
    
    QAction* action = qobject_cast<QAction*>(sender());
    QString toto(action->data().toString());
    
    emit recall_Snapshot(toto, true);
}

void FLWindow::initNavigateMenu(QList<QAction*> wins){
        
    QList<QAction*>::iterator it;
    for(it = wins.begin(); it != wins.end() ; it++){
        fNavigateMenu->addAction(*it);
    }
}

void FLWindow::updateNavigateMenu(QList<QAction*> wins){
    
    fNavigateMenu->clear();
    initNavigateMenu(wins);
}

void FLWindow::frontShowFromMenu(){
    
    QAction* action = qobject_cast<QAction*>(sender());

    emit front(action->data().toString());
}

//Redirection of a received error
void FLWindow::errorPrint(const char* msg){
    emit error(msg);
}

int FLWindow::RemoteDSPErrorCallback(int error_code, void* arg){
    
#ifdef REMOTE
    QDateTime currentTime(QDateTime::currentDateTime());
    
    FLWindow* errorWin = (FLWindow*) arg;
    
    if(errorWin->fLastMigration.secsTo(currentTime) > 3){
        
        if(error_code == WRITE_ERROR || error_code == READ_ERROR){
            
            errorWin->errorPrint("Remote Connection Error.\n Switching back to local processing.");
            
            errorWin->fMenu->setNewOptions("localhost", 0, "local processing");
            errorWin->redirectSwitch("localhost", 0);
        }
    }
    
    errorWin->fLastMigration = currentTime;
#endif
    return -1;
}



