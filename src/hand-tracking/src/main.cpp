#include <chrono>
#include <future>
#include <iostream>
#include <memory>

#include <BayesFiltersLib/FilteringFunction.h>
#include <BayesFiltersLib/SIRParticleFilter.h>
#include <yarp/os/ConstString.h>
#include <yarp/os/LogStream.h>
#include <yarp/os/Network.h>
#include <yarp/os/ResourceFinder.h>
#include <yarp/os/Value.h>
#include <opencv2/core/core.hpp>
#include <opencv2/core/cuda.hpp>

#include "BrownianMotion.h"
#include "DrawPoseParticle.h"
#include "iCubFwdKinMotion.h"
#include "playFwdKinMotion.h"
#include "VisualProprioception.h"
#include "VisualParticleFilterCorrection.h"
#include "VisualSIRParticleFilter.h"

using namespace bfl;
using namespace cv;
using namespace Eigen;
using namespace yarp::dev;
using namespace yarp::os;


/* UTILITY FUNCTIONS FOREWORD DECLARATIONS */
std::string engine_count_to_string(int engine_count);


/* MAIN */
int main(int argc, char *argv[])
{
    ConstString log_ID = "[Main]";
    yInfo() << log_ID << "Configuring and starting module...";

    Network yarp;
    if (!yarp.checkNetwork(3.0))
    {
        yError() << "YARP seems unavailable!";
        return EXIT_FAILURE;
    }

    // FIXME: page locked dovrebbe essere più veloce da utilizzate con CUDA, non sembra essere il caso.
//    Mat::setDefaultAllocator(cuda::HostMem::getAllocator(cuda::HostMem::PAGE_LOCKED));

    cuda::DeviceInfo gpu_dev;
    yInfo() << log_ID << "[CUDA] Engine capability:"              << engine_count_to_string(gpu_dev.asyncEngineCount());
    yInfo() << log_ID << "[CUDA] Can have concurrent kernel:"     << gpu_dev.concurrentKernels();
    yInfo() << log_ID << "[CUDA] Streaming multiprocessor count:" << gpu_dev.multiProcessorCount();
    yInfo() << log_ID << "[CUDA] Can map host memory:"            << gpu_dev.canMapHostMemory();
    yInfo() << log_ID << "[CUDA] Clock:"                          << gpu_dev.clockRate() << "KHz";

    ResourceFinder rf;
    rf.setVerbose();
    rf.setDefaultContext("hand-tracking");
    rf.setDefaultConfigFile("parameters.ini");
    rf.configure(argc, argv);

    ConstString robot_name       = rf.find("robot").asString();
    ConstString robot_cam_sel    = rf.find("cam").asString();
    ConstString robot_laterality = rf.find("laterality").asString();
    bool        play             = rf.find("play").asBool();
    int         num_particles    = rf.findGroup("PF").check("num_particles", Value(50)).asInt();

    if (robot_name.empty())
        robot_name       = "icub";
    if (robot_cam_sel.empty())
        robot_cam_sel    = "left";
    if (robot_laterality.empty())
        robot_laterality = "right";

    yInfo() << log_ID << "Running with:";
    yInfo() << log_ID << " - robot name:"          << robot_name;
    yInfo() << log_ID << " - robot camera:"        << robot_cam_sel;
    yInfo() << log_ID << " - robot laterality:"    << robot_laterality;
    yInfo() << log_ID << " - use data from ports:" << (play ? "true" : "false");
    yInfo() << log_ID << " - number of particles:" << num_particles;

    /* Initialize filtering functions */
    // FIXME: passare robot name ai metodi
    std::unique_ptr<BrownianMotion> brown(new BrownianMotion(0.005, 0.005, 3.0, 1.5, 1));
    std::unique_ptr<StateModel>     icub_motion;
    if (!play)
    {
        std::unique_ptr<iCubFwdKinMotion> icub_fwdkin(new iCubFwdKinMotion(std::move(brown), robot_name, robot_laterality, robot_cam_sel));
        icub_motion = std::move(icub_fwdkin);
    }
    else
    {
        std::unique_ptr<playFwdKinMotion> play_fwdkin(new playFwdKinMotion(std::move(brown), robot_name, robot_laterality, robot_cam_sel));
        icub_motion = std::move(play_fwdkin);
    }


    std::unique_ptr<DrawPoseParticle> pf_prediction(new DrawPoseParticle(std::move(icub_motion)));

    std::unique_ptr<VisualProprioception> proprio;
    try
    {
        std::unique_ptr<VisualProprioception> vp(new VisualProprioception(num_particles / gpu_dev.multiProcessorCount(), robot_cam_sel, robot_laterality, rf.getContext()));

        proprio = std::move(vp);
        num_particles = proprio->getOGLTilesRows() * proprio->getOGLTilesCols() * gpu_dev.multiProcessorCount();
    }
    catch (const std::runtime_error& e)
    {
        yError() << e.what();
        return EXIT_FAILURE;
    }

    std::unique_ptr<VisualParticleFilterCorrection> vpf_correction(new VisualParticleFilterCorrection(std::move(proprio), gpu_dev.multiProcessorCount()));

    std::unique_ptr<Resampling> resampling(new Resampling());

    VisualSIRParticleFilter vsir_pf(std::move(pf_prediction), std::move(vpf_correction),
                                    std::move(resampling),
                                    robot_cam_sel, robot_laterality, num_particles);

    std::future<void> thr_vpf = vsir_pf.spawn();
    while (vsir_pf.isRunning()) glfwWaitEventsTimeout(1.0);

    yInfo() << log_ID << "Joining filthering thread...";
    while (thr_vpf.wait_for(std::chrono::milliseconds(1)) == std::future_status::timeout) glfwPollEvents();

    glfwMakeContextCurrent(NULL);
    glfwTerminate();

    yInfo() << log_ID << "Main returning.";
    yInfo() << log_ID << "Application closed.";

    return EXIT_SUCCESS;
}


/* UTILITY FUNCTIONS */
std::string engine_count_to_string(int engine_count)
{
    if (engine_count == 0) return "concurrency is unsupported on this device";
    if (engine_count == 1) return "the device can concurrently copy memory between host and device while executing a kernel";
    if (engine_count == 2) return "the device can concurrently copy memory between host and device in both directions and execute a kernel at the same time";
    return "wrong argument...!";
}