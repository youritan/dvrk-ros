/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-    */
/* ex: set filetype=cpp softtabstop=4 shiftwidth=4 tabstop=4 cindent expandtab: */

/*
  Author(s):  Zihan Chen
  Created on: 2013-02-07

  (C) Copyright 2013-2014 Johns Hopkins University (JHU), All Rights Reserved.

--- begin cisst license - do not edit ---

This software is provided "as is" under an open source license, with
no warranty.  The complete license can be found in license.txt and
http://www.cisst.org/cisst/license.txt.

--- end cisst license ---

*/

// system
#include <iostream>
#include <map>

// cisst/saw
#include <cisstCommon/cmnPath.h>
#include <cisstCommon/cmnCommandLineOptions.h>
#include <cisstOSAbstraction/osaSleep.h>
#include <cisstMultiTask/mtsQtApplication.h>
#include <sawRobotIO1394/mtsRobotIO1394.h>
#include <sawRobotIO1394/mtsRobotIO1394QtWidgetFactory.h>
#include <sawControllers/mtsPIDQtWidget.h>
#include <sawIntuitiveResearchKit/mtsIntuitiveResearchKitConsole.h>
#include <sawIntuitiveResearchKit/mtsIntuitiveResearchKitConsoleQtWidget.h>
#include <sawIntuitiveResearchKit/mtsIntuitiveResearchKitArmQtWidget.h>

#include <QTabWidget>

#include <ros/ros.h>
#include <cisst_ros_bridge/mtsROSBridge.h>

int main(int argc, char ** argv)
{
    // log configuration
    cmnLogger::SetMask(CMN_LOG_ALLOW_ALL);
    cmnLogger::SetMaskDefaultLog(CMN_LOG_ALLOW_ALL);
    cmnLogger::SetMaskFunction(CMN_LOG_ALLOW_ALL);
    cmnLogger::SetMaskClass("mtsIntuitiveResearchKitECM", CMN_LOG_ALLOW_ALL);
    cmnLogger::AddChannel(std::cerr, CMN_LOG_ALLOW_ERRORS_AND_WARNINGS);

    // ---- WARNING: hack to remove ros args ----
    ros::V_string argout;
    ros::removeROSArgs(argc, argv, argout);
    argc = argout.size();
    // ------------------------------------------

    // parse options
    cmnCommandLineOptions options;
    int firewirePort = 0;
    std::string gcmip = "-1";
    typedef std::map<std::string, std::string> ConfigFilesType;
    ConfigFilesType configFiles;
    std::string armName;

    options.AddOptionOneValue("i", "io",
                              "configuration file for robot IO (see sawRobotIO1394)",
                              cmnCommandLineOptions::REQUIRED_OPTION, &configFiles["io"]);
    options.AddOptionOneValue("p", "pid",
                              "configuration file for PID controller (see sawControllers, mtsPID)",
                              cmnCommandLineOptions::REQUIRED_OPTION, &configFiles["pid"]);
    options.AddOptionOneValue("k", "kinematic",
                              "configuration file for kinematic (see cisstRobot, robManipulator)",
                              cmnCommandLineOptions::REQUIRED_OPTION, &configFiles["kinematic"]);
    options.AddOptionOneValue("n", "arm-name",
                              "arm name, i.e. PSM1, ... as found in sawRobotIO configuration file",
                              cmnCommandLineOptions::REQUIRED_OPTION, &armName);
    options.AddOptionOneValue("f", "firewire",
                              "firewire port number(s)",
                              cmnCommandLineOptions::OPTIONAL_OPTION, &firewirePort);
    options.AddOptionOneValue("g", "gcmip",
                              "global component manager IP address",
                              cmnCommandLineOptions::OPTIONAL_OPTION, &gcmip);

    std::string errorMessage;
    if (!options.Parse(argc, argv, errorMessage)) {
        std::cerr << "Error: " << errorMessage << std::endl;
        options.PrintUsage(std::cerr);
        return -1;
    }

    for (ConfigFilesType::const_iterator iter = configFiles.begin();
         iter != configFiles.end();
         ++iter) {
        if (!cmnPath::Exists(iter->second)) {
            std::cerr << "File not found for " << iter->first
                      << ": " << iter->second << std::endl;
            return -1;
        } else {
            std::cout << "Configuration file for " << iter->first
                      << ": " << iter->second << std::endl;
        }
    }
    std::cout << "FirewirePort: " << firewirePort << std::endl;

    std::string processname = "dv-arm";
    mtsManagerLocal * componentManager = 0;
    if (gcmip != "-1") {
        try {
            componentManager = mtsManagerLocal::GetInstance(gcmip, processname);
        } catch(...) {
            std::cerr << "Failed to get GCM instance." << std::endl;
            return -1;
        }
    } else {
        componentManager = mtsManagerLocal::GetInstance();
    }

    // create a Qt application and tab to hold all widgets
    mtsQtApplication * qtAppTask = new mtsQtApplication("QtApplication", argc, argv);
    qtAppTask->Configure();
    componentManager->AddComponent(qtAppTask);


    // console
    mtsIntuitiveResearchKitConsole * console = new mtsIntuitiveResearchKitConsole("console");
    componentManager->AddComponent(console);
    mtsIntuitiveResearchKitConsoleQtWidget * consoleGUI = new mtsIntuitiveResearchKitConsoleQtWidget("consoleGUI");
    componentManager->AddComponent(consoleGUI);
    // connect console GUI to console
    componentManager->Connect("console", "Main", "consoleGUI", "Main");

    // IO
    mtsRobotIO1394 * io = new mtsRobotIO1394("io", 1.0 * cmn_ms, firewirePort);
    io->Configure(configFiles["io"]);
    componentManager->AddComponent(io);

    // Create the arm
    unsigned int numberOfAxis = 0;
    mtsIntuitiveResearchKitConsole::Arm * arm
            = new mtsIntuitiveResearchKitConsole::Arm(armName, io->GetName());
    arm->ConfigurePID(configFiles["pid"]);
    // Configure based on arm type assuming name
    if ((armName == "PSM1") || (armName == "PSM2") || (armName == "PSM3")) {
        arm->ConfigureArm(mtsIntuitiveResearchKitConsole::Arm::ARM_PSM,
                          configFiles["kinematic"], 3.0 * cmn_ms);
        numberOfAxis = 7;
    } else if ((armName == "MTML") || (armName == "MTMR")) {
        arm->ConfigureArm(mtsIntuitiveResearchKitConsole::Arm::ARM_MTM,
                          configFiles["kinematic"], 3.0 * cmn_ms);
        numberOfAxis = 8;
    } else if (armName == "ECM") {
        arm->ConfigureArm(mtsIntuitiveResearchKitConsole::Arm::ARM_ECM,
                          configFiles["kinematic"], 3.0 * cmn_ms);
        numberOfAxis = 4;
    } else {
        std::cerr << "Arm name should be either PSM1, PSM2, PSM3, MTML, MTMR or ECM, not " << armName << std::endl;
        return -1;
    }
    console->AddArm(arm);

    // connect ioGUIMaster to io
    mtsRobotIO1394QtWidgetFactory * robotWidgetFactory = new mtsRobotIO1394QtWidgetFactory("robotWidgetFactory");
    componentManager->AddComponent(robotWidgetFactory);
    componentManager->Connect("robotWidgetFactory", "RobotConfiguration", "io", "Configuration");
    robotWidgetFactory->Configure();

    // PID GUI
    mtsPIDQtWidget * pidGUI = new mtsPIDQtWidget("PID", numberOfAxis);
    pidGUI->Configure();
    componentManager->AddComponent(pidGUI);
    componentManager->Connect(pidGUI->GetName(), "Controller", arm->PIDComponentName(), "Controller");

    // GUI
    mtsIntuitiveResearchKitArmQtWidget * armGUI = new mtsIntuitiveResearchKitArmQtWidget("Arm");
    armGUI->Configure();
    componentManager->AddComponent(armGUI);
    componentManager->Connect(armGUI->GetName(), "Manipulator", arm->Name(), "Robot");

    // organize all widgets in a tab widget
    QTabWidget * tabWidget = new QTabWidget;
    tabWidget->addTab(consoleGUI, "Main");
    tabWidget->addTab(armGUI, "Arm");
    tabWidget->addTab(pidGUI, "PID");
    mtsRobotIO1394QtWidgetFactory::WidgetListType::const_iterator iterator;
    for (iterator = robotWidgetFactory->Widgets().begin();
         iterator != robotWidgetFactory->Widgets().end();
         ++iterator) {
        tabWidget->addTab(*iterator, (*iterator)->GetName().c_str());
    }
    tabWidget->addTab(robotWidgetFactory->ButtonsWidget(), "Buttons");
    tabWidget->show();


    //-------------------------------------------------------
    // Start ROS Bridge
    // ------------------------------------------------------

    // ros wrapper
    mtsROSBridge robotBridge("ROSECM", 5 * cmn_ms, true);

    // connect to ECM
    robotBridge.AddPublisherFromReadCommand<prmPositionCartesianGet, geometry_msgs::Pose>(
                "Robot", "GetPositionCartesian", "/dvrk_ecm/cartesian_pose_current");
    robotBridge.AddSubscriberToWriteCommand<std::string, std_msgs::String>(
                "Robot", "SetRobotControlState", "/dvrk_ecm/set_robot_state");
    robotBridge.AddSubscriberToWriteCommand<prmPositionCartesianSet, geometry_msgs::Pose>(
                "Robot", "SetPositionCartesian", "/dvrk_ecm/set_position_cartesian");

    robotBridge.AddPublisherFromEventWrite<prmEventButton, std_msgs::Bool>(
                "Robot","ManipClutchBtn","/dvrk_ecm/manip_clutch");
    robotBridge.AddPublisherFromEventWrite<prmEventButton, std_msgs::Bool>(
                "Robot","SUJClutchBtn","/dvrk_ecm/suj_clutch");

    // PID
    robotBridge.AddPublisherFromReadCommand<prmPositionJointGet, sensor_msgs::JointState>(
                "PID", "GetPositionJoint", "/dvrk_ecm/joint_position_current");
    robotBridge.AddPublisherFromReadCommand<vctDoubleVec, cisst_msgs::vctDoubleVec>(
                "PID", "GetEffortJointDesired", "/dvrk_ecm/joint_effort_current");
    robotBridge.AddSubscriberToWriteCommand<prmForceTorqueJointSet , sensor_msgs::JointState>(
                "PID", "SetTorqueJoint", "/dvrk_ecm/set_joint_effort");
    robotBridge.AddSubscriberToWriteCommand<prmPositionJointSet, sensor_msgs::JointState>(
                "PID", "SetPositionJoint","/dvrk_ecm/set_position_joint");


    // Connect
    componentManager->AddComponent(&robotBridge);
    componentManager->Connect(robotBridge.GetName(), "Robot", arm->Name(), "Robot");
    componentManager->Connect(robotBridge.GetName(), "PID", arm->PIDComponentName(), "Controller");

    //-------------------------------------------------------
    // End ROS Bridge
    // ------------------------------------------------------



    //-------------- create the components ------------------
    io->CreateAndWait(2.0 * cmn_s); // this will also create the pids as they are in same thread
    io->StartAndWait(2.0 * cmn_s);
    componentManager->GetComponent(arm->PIDComponentName())->StartAndWait(2.0 * cmn_s);

    // start all other components
    componentManager->CreateAllAndWait(2.0 * cmn_s);
    componentManager->StartAllAndWait(2.0 * cmn_s);

    // QtApplication will run in main thread and return control
    // when exited.

    componentManager->KillAllAndWait(2.0 * cmn_s);
    componentManager->Cleanup();

    // delete dvgc robot
    delete pidGUI;
    delete io;
    delete robotWidgetFactory;

    // stop all logs
    cmnLogger::Kill();

    return 0;
}
