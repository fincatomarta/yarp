/*
 * SPDX-FileCopyrightText: 2006-2021 Istituto Italiano di Tecnologia (IIT)
 * SPDX-FileCopyrightText: 2006-2010 RobotCub Consortium
 * SPDX-License-Identifier: BSD-3-Clause
 */

// The main body of yarprun is now part of the YARP_run library, in:
//   src/libYARP_run/src/yarp/run/Run.cpp

#include <yarp/run/Run.h>
#include <yarp/os/Network.h>
#include <yarp/os/LogStream.h>
#include <cstdio>
#include <string>
#include <vector>
#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace {

std::string shellQuote(const std::string& value)
{
    std::string quoted = "'";
    for (const auto ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

std::vector<std::string> getCleanedCommand(int argc, char* argv[])
{
    std::vector<std::string> cleanArgs;
    cleanArgs.push_back("yarprun");

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--docker") {
            ++i;
            continue;
        }

        if (arg == "--conda") {
            ++i;
            continue;
        }

        if (arg == "--pixi") {
            continue;
        }

        cleanArgs.push_back(arg);
    }

    return cleanArgs;
}

int countEnvironmentModes(const std::string& dockerName, const std::string& envName, bool usePixi)
{
    int modes = 0;
    if (!dockerName.empty()) { ++modes; }
    if (!envName.empty()) { ++modes; }
    if (usePixi) { ++modes; }

    return modes;
}

std::vector<std::string> getDockerCommand(const std::string& dockerName, const std::vector<std::string>& cleanArgs)
{
    std::string innerCommand = "exec";
    for (const auto& arg : cleanArgs) {
        innerCommand += " ";
        innerCommand += shellQuote(arg);
    }

    std::string command;
    command += "docker start ";
    command += shellQuote(dockerName);
    command += " && exec docker exec -it ";
    command += shellQuote(dockerName);
    command += " bash -lc ";
    command += shellQuote(innerCommand);

    std::vector<std::string> execArgs;
    execArgs.push_back("bash");
    execArgs.push_back("-lc");
    execArgs.push_back(command);
    return execArgs;
}

std::vector<std::string> getPixiCommand(const std::vector<std::string>& cleanArgs)
{
    std::vector<std::string> execArgs;
    execArgs.push_back("pixi");
    execArgs.push_back("run");
    execArgs.insert(execArgs.end(), cleanArgs.begin(), cleanArgs.end());
    return execArgs;
}

std::vector<std::string> getCondaCommand(const std::string& envName, const std::vector<std::string>& cleanArgs)
{
    std::string command;
    command += "source \"$(conda info --base)/etc/profile.d/conda.sh\"";
    command += " && conda activate ";
    command += shellQuote(envName);
    command += " && exec";

    for (const auto& arg : cleanArgs) {
        command += " ";
        command += shellQuote(arg);
    }

    std::vector<std::string> execArgs;
    execArgs.push_back("bash");
    execArgs.push_back("-lc");
    execArgs.push_back(command);

    return execArgs;
}

int sendCommand(const std::vector<std::string>& execArgs)
{
#if defined(_WIN32)
    YARP_UNUSED(execArgs);
    yError() << "--docker, --pixi and --conda are not supported on Windows yet";
    return 1;
#else
    std::vector<char*> cargs;
    for (const auto& s : execArgs) {
        cargs.push_back(const_cast<char*>(s.c_str()));
    }
    cargs.push_back(nullptr);

    execvp(cargs[0], cargs.data());

    perror("ERROR: execvp failed");
    return 1;
#endif
}

static int maybeRelaunchInEnvironment(int argc, char* argv[])
{
    // Constructing the cleaned command
    std::vector<std::string> cleanArgs = getCleanedCommand(argc, argv);

    bool hasServer = false;
    bool usePixi = false;
    std::string dockerName;
    std::string envName;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--server") {
            hasServer = true;
        }
        else if (arg == "--docker")
        {
            if (i + 1 >= argc) {
                yError() << "Missing argument for --docker";
                return 1;
            }
            dockerName = argv[i+1];
            ++i;
        }
        else if (arg == "--conda"){
            if (i+1 >= argc) {
                yError() << "Missing argument for --conda";
                return 1;
            }
            envName = argv[i+1];
            ++i;
        }
        else if (arg == "--pixi"){
            usePixi = true;
        }
    }

    int modes = countEnvironmentModes(dockerName, envName, usePixi);
    if (modes == 0) {
        return -1;
    }

    if (modes > 1) {
        yError() << "Use only one of --docker, --pixi or --conda";
        return 1;
    }

    if (!hasServer)
    {
        yError() << "--docker, --pixi and --conda are supported only with --server";
        return 1;
    }

    if (!dockerName.empty()) {
        return sendCommand(getDockerCommand(dockerName, cleanArgs));
    }

    if (!envName.empty()) {
        return sendCommand(getCondaCommand(envName, cleanArgs));
    }

    if (usePixi) {
#if defined(_WIN32)
        yError() << "--pixi is not supported on Windows yet";
        return 1;
#else
        if (access("pixi.toml", F_OK) != 0) {
            yError() << "--pixi requires a pixi.toml in the current directory";
            return 1;
        }
#endif
        return sendCommand(getPixiCommand(cleanArgs));
    }

    return -1;
}

} // namespace

int main(int argc, char *argv[])
{

    int relaunchResult = maybeRelaunchInEnvironment(argc, argv);
    if (relaunchResult >= 0)
    {
        return relaunchResult;
    }
    yarp::os::Network yarp;

    if (!yarp.checkNetwork())
    {
        yError("Sorry YARP network does not seem to be available, is the yarp server available?\n");
        return 1;
    }

    return yarp::run::Run::main(argc,argv);
}
