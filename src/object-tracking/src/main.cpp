#include <Correction.h>
#include <GaussianFilter_.h>
#include <iCubPointCloud.h>
#include <KinematicModel.h>
#include <NanoflannPointCloudPrediction.h>
#include <Random3DPose.h>
#include <SimulatedFilter.h>
#include <SimulatedPointCloud.h>

#include <BayesFilters/AdditiveMeasurementModel.h>
#include <BayesFilters/GaussianCorrection.h>
// #include <BayesFilters/GaussianFilter.h>
#include <BayesFilters/GaussianPrediction.h>
#include <BayesFilters/KFPrediction.h>

#include <yarp/os/LogStream.h>
#include <yarp/os/ResourceFinder.h>

#include <Eigen/Dense>

#include <cstdlib>
#include <string>
#include <sstream>

using namespace bfl;
using namespace Eigen;
using namespace yarp::os;

VectorXd loadVectorDouble
(
    ResourceFinder &rf,
    const std::string key,
    const std::size_t size
)
{
    bool ok = true;

    if (rf.find(key).isNull())
        ok = false;

    Bottle* b = rf.find(key).asList();
    if (b == nullptr)
        ok = false;

    if (b->size() != size)
        ok = false;

    if (!ok)
    {
        yError() << "[Main]" << "Unable to load vector" << key;
        std::exit(EXIT_FAILURE);
    }

    VectorXd vector(size);
    for (std::size_t i = 0; i < b->size(); i++)
    {
        Value item_v = b->get(i);
        if (item_v.isNull())
            return VectorXd(0);

        if (!item_v.isDouble())
            return VectorXd(0);

        vector(i) = item_v.asDouble();
    }

    return vector;
}


std::string eigenToString(VectorXd& v)
{
    std::stringstream ss;
    ss << v.transpose();
    return ss.str();
}


int main(int argc, char** argv)
{
    const std::string log_ID = "[Main]";
    yInfo() << log_ID << "Configuring and starting module...";

    const std::string port_prefix = "object-tracking";

    ResourceFinder rf;
    rf.setVerbose();
    rf.setDefaultContext("object-tracking");
    rf.setDefaultConfigFile("config.ini");
    rf.configure(argc, argv);

    /* Get mode of operation. */
    ResourceFinder rf_mode_parameters = rf.findNestedResourceFinder("MODE");
    const std::string mode  = rf_mode_parameters.check("mode",  Value("simulation")).asString();
    const std::string robot = rf_mode_parameters.check("robot", Value("icubSim")).asString();

    std::unique_ptr<Network> yarp;
    if (mode != "simulation")
    {
        yarp = std::move(std::unique_ptr<Network>(new Network()));

        if (!yarp->checkNetwork())
        {
            yError() << log_ID << "YARP seems unavailable!";
            return EXIT_FAILURE;
        }
    }

    /* Get initial condition. */
    ResourceFinder rf_initial_conditions = rf.findNestedResourceFinder("INITIAL_CONDITION");
    VectorXd x_0           = loadVectorDouble(rf_initial_conditions, "x_0",           3);
    VectorXd v_0           = loadVectorDouble(rf_initial_conditions, "v_0",           3);
    VectorXd euler_0       = loadVectorDouble(rf_initial_conditions, "euler_0",       3);
    VectorXd euler_dot_0   = loadVectorDouble(rf_initial_conditions, "euler_dot_0",   3);
    VectorXd cov_x_0       = loadVectorDouble(rf_initial_conditions, "cov_x_0",       3);
    VectorXd cov_v_0       = loadVectorDouble(rf_initial_conditions, "cov_v_0",       3);
    VectorXd cov_eul_0     = loadVectorDouble(rf_initial_conditions, "cov_eul_0",     3);
    VectorXd cov_eul_dot_0 = loadVectorDouble(rf_initial_conditions, "cov_eul_dot_0", 3);

    /* Kinematic model. */
    ResourceFinder rf_kinematic_model = rf.findNestedResourceFinder("KINEMATIC_MODEL");
    double sample_time     = rf_kinematic_model.check("sample_time", Value("1.0")).asDouble();
    VectorXd kin_psd_acc   = loadVectorDouble(rf_kinematic_model, "psd_acc", 3);
    VectorXd kin_psd_euler = loadVectorDouble(rf_kinematic_model, "psd_euler_acc", 3);

    /* Measurement model. */
    ResourceFinder rf_measurement_model = rf.findNestedResourceFinder("MEASUREMENT_MODEL");
    VectorXd noise_covariance = loadVectorDouble(rf_measurement_model, "noise_covariance", 3);
    MatrixXd noise_covariance_diagonal = noise_covariance.asDiagonal();

    /* Unscented transform. */
    ResourceFinder rf_unscented_transform = rf.findNestedResourceFinder("UNSCENTED_TRANSFORM");
    double ut_alpha = rf_unscented_transform.check("alpha", Value("1.0")).asDouble();
    double ut_beta  = rf_unscented_transform.check("beta", Value("2.0")).asDouble();
    double ut_kappa = rf_unscented_transform.check("kappa", Value("0.0")).asDouble();

    /* Point cloud prediction. */
    ResourceFinder rf_point_cloud_prediction = rf.findNestedResourceFinder("POINT_CLOUD_PREDICTION");
    std::size_t pc_pred_num_samples = rf_point_cloud_prediction.check("number_samples", Value("100")).asInt();

    /* Simulation parameters. */
    double sim_sample_time;
    double sim_duration;
    VectorXd sim_psd_acc;
    VectorXd sim_psd_w;
    VectorXd sim_x_0;
    VectorXd sim_v_0;
    VectorXd sim_q_0;
    VectorXd sim_w_0;
    //
    std::size_t sim_point_cloud_number;
    VectorXd sim_point_cloud_observer;
    VectorXd sim_point_cloud_noise_std;
    bool sim_point_cloud_back_culling;
    if (mode == "simulation")
    {
        ResourceFinder rf_simulation = rf.findNestedResourceFinder("SIMULATION");
        sim_sample_time = rf_simulation.check("sample_time", Value("1.0")).asDouble();
        sim_duration    = rf_simulation.check("duration", Value("1.0")).asDouble();
        sim_psd_acc     = loadVectorDouble(rf_simulation, "psd_acc", 3);
        sim_psd_w       = loadVectorDouble(rf_simulation, "psd_w", 3);
        sim_x_0         = loadVectorDouble(rf_simulation, "x_0", 3);
        sim_v_0         = loadVectorDouble(rf_simulation, "v_0", 3);
        sim_q_0         = loadVectorDouble(rf_simulation, "q_0", 4);
        sim_w_0         = loadVectorDouble(rf_simulation, "w_0", 3);

        ResourceFinder rf_sim_point_cloud = rf.findNestedResourceFinder("SIMULATED_POINT_CLOUD");
        sim_point_cloud_number       = rf_sim_point_cloud.check("number_points", Value("1000")).asInt();
        sim_point_cloud_observer     = loadVectorDouble(rf_sim_point_cloud, "observer_origin", 3);
        sim_point_cloud_noise_std    = loadVectorDouble(rf_sim_point_cloud, "noise_std", 3);
        sim_point_cloud_back_culling = rf_sim_point_cloud.check("back_cullnig", Value(true)).asBool();
    }

    /* Mesh parameters. */
    ResourceFinder rf_object = rf.findNestedResourceFinder("OBJECT");
    const std::string object_name          = rf_object.check("object_name", Value("ycb_mustard")).asString();
    const std::string iol_object_name      = rf_object.check("iol_object_name", Value("mustard")).asString();
    const std::string object_mesh_path_obj = rf.findPath("mesh/" + object_name) + "/nontextured.obj";
    const std::string object_mesh_path_ply = rf.findPath("mesh/" + object_name) + "/nontextured.ply";
    bool use_bbox_0                        = rf_object.check("use_bbox_0", Value(false)).asBool();
    VectorXd bbox_tl_0;
    VectorXd bbox_br_0;
    if (use_bbox_0)
    {
        bbox_tl_0 = loadVectorDouble(rf_object, "bbox_tl_0", 2);
        bbox_br_0 = loadVectorDouble(rf_object, "bbox_br_0", 2);
    }

    /* Logging parameters. */
    ResourceFinder rf_logging = rf.findNestedResourceFinder("LOG");
    bool enable_log = rf_logging.check("enable_log", Value(false)).asBool();
    const std::string log_path = rf_logging.check("absolute_log_path", Value("")).asString();
    if (enable_log && log_path == "")
    {
        yWarning() << "Invalid log path. Disabling log...";
        enable_log = false;
    }

    /* Log parameters. */
    yInfo() << log_ID << "Mode of operation:";
    yInfo() << log_ID << "- mode:"  << mode;
    yInfo() << log_ID << "- robot:" << robot;

    yInfo() << log_ID << "Initial conditions:";
    yInfo() << log_ID << "- x_0: "           << eigenToString(x_0);
    yInfo() << log_ID << "- v_0: "           << eigenToString(v_0);
    yInfo() << log_ID << "- euler_0: "       << eigenToString(euler_0);
    yInfo() << log_ID << "- euler_dot_0: "   << eigenToString(euler_0);
    yInfo() << log_ID << "- cov_x_0: "       << eigenToString(cov_x_0);
    yInfo() << log_ID << "- cov_v_0: "       << eigenToString(cov_v_0);
    yInfo() << log_ID << "- cov_eul_0: "     << eigenToString(cov_eul_0);
    yInfo() << log_ID << "- cov_eul_dot_0: " << eigenToString(cov_eul_dot_0);

    yInfo() << log_ID << "Kinematic model:";
    yInfo() << log_ID << "- sample_time:"   << sample_time;
    yInfo() << log_ID << "- psd_acc:"       << eigenToString(kin_psd_acc);
    yInfo() << log_ID << "- psd_euler_acc:" << eigenToString(kin_psd_euler);

    yInfo() << log_ID << "Measurement model:";
    yInfo() << log_ID << "- noise_covariance:" << eigenToString(noise_covariance);

    yInfo() << log_ID << "Unscented transform:";
    yInfo() << log_ID << "- alpha:" << ut_alpha;
    yInfo() << log_ID << "- beta:"  << ut_beta;
    yInfo() << log_ID << "- kappa:" << ut_kappa;

    yInfo() << log_ID << "Point cloud prediction:";
    yInfo() << log_ID << "- num_samples:" << pc_pred_num_samples;

    if (mode == "simulation")
    {
        yInfo() << log_ID << "Simulation:";
        yInfo() << log_ID << "- sample_time:" << sim_sample_time;
        yInfo() << log_ID << "- duration:"    << sim_duration;
        yInfo() << log_ID << "- psd_acc:"     << eigenToString(sim_psd_acc);
        yInfo() << log_ID << "- psd_w:"       << eigenToString(sim_psd_w);
        yInfo() << log_ID << "- x_0:"         << eigenToString(sim_x_0);
        yInfo() << log_ID << "- v_0:"         << eigenToString(sim_v_0);
        yInfo() << log_ID << "- q_0:"         << eigenToString(sim_q_0);
        yInfo() << log_ID << "- w_0:"         << eigenToString(sim_w_0);

        yInfo() << log_ID << "Simulated point cloud:";
        yInfo() << log_ID << "- number_points:"   << sim_point_cloud_number;
        yInfo() << log_ID << "- observer_origin:" << eigenToString(sim_point_cloud_observer);
        yInfo() << log_ID << "- noise_std:"       << eigenToString(sim_point_cloud_noise_std);
        yInfo() << log_ID << "- back_culling:"    << sim_point_cloud_back_culling;
    }

    yInfo() << log_ID << "Object:";
    yInfo() << log_ID << "- object_name:"        << object_name;
    yInfo() << log_ID << "- mesh path is (obj):" << object_mesh_path_obj;
    yInfo() << log_ID << "- mesh path is (ply):" << object_mesh_path_ply;
    yInfo() << log_ID << "- iol_object_name:"    << iol_object_name;
    yInfo() << log_ID << "- use_bbox_0:"         << use_bbox_0;
    if (use_bbox_0)
    {
        yInfo() << log_ID << "- bbox_tl_0:" << eigenToString(bbox_tl_0);
        yInfo() << log_ID << "- bbox_br_0:" << eigenToString(bbox_br_0);
    }

    yInfo() << log_ID << "Logging:";
    yInfo() << log_ID << "- enable_log:"        << enable_log;
    yInfo() << log_ID << "- absolute_log_path:" << log_path;

    /**
     * Prepare pointer to the measurement model.
     */


    /**
     * Initialize point cloud prediction.
     */
    std::unique_ptr<PointCloudPrediction> pc_prediction =
        std::unique_ptr<NanoflannPointCloudPrediction>(new NanoflannPointCloudPrediction(object_mesh_path_ply, pc_pred_num_samples));


    /**
     * Initialize measurement model.
     */
    std::unique_ptr<AdditiveMeasurementModel> measurement_model;
    std::shared_ptr<iCubPointCloudExogenousData> icub_pc_exog_data = std::make_shared<iCubPointCloudExogenousData>();
    if (mode == "simulation")
    {
        /**
         * Initial condition of simulation.
         */
        VectorXd state_0(3 + 3 + 4 + 3);
        state_0.head(3) = sim_x_0;
        state_0.segment(3, 3) = sim_v_0;
        state_0.segment(6, 4) = sim_q_0;
        state_0.tail(3) = sim_w_0;

        /**
         * Initialize random pose generator.
         */
        std::unique_ptr<StateModel> rand_pose(
            new Random3DPose(sim_sample_time,
                             sim_psd_acc(0), sim_psd_acc(1), sim_psd_acc(2),
                             sim_psd_w(0), sim_psd_w(1), sim_psd_w(2)));

        /**
         * Initialize simulated state model.
         */
        std::unique_ptr<SimulatedStateModel> sim_rand_pose(
            new SimulatedStateModel(std::move(rand_pose), state_0, sim_duration / sim_sample_time));
        if (enable_log)
            sim_rand_pose->enable_log(log_path, "object-tracking");

        /**
         * Initialize simulated measurement model.
         */
        std::unique_ptr<SimulatedPointCloud> pc_simulation =
            std::unique_ptr<SimulatedPointCloud>(new SimulatedPointCloud(object_mesh_path_ply,
                                                                         std::move(pc_prediction),
                                                                         std::move(sim_rand_pose),
                                                                         // This is the modeled noise covariance
                                                                         noise_covariance_diagonal,
                                                                         sim_point_cloud_observer,
                                                                         sim_point_cloud_number,
                                                                         sim_point_cloud_back_culling));
        // This the actual simulated noise standard deviation
        pc_simulation->enableNoise(sim_point_cloud_noise_std(0),
                                   sim_point_cloud_noise_std(1),
                                   sim_point_cloud_noise_std(2));

        // Prefetch measurements
        std::cout << "Prefetching measurements for faster simulation..." << std::flush;
        pc_simulation->prefetchMeasurements(sim_duration / sim_sample_time);
        std::cout << "done." << std::endl;

        measurement_model = std::move(pc_simulation);
    }
    else
    {
        std::unique_ptr<iCubPointCloud> pc_icub;
        if (use_bbox_0)
        {
            std::pair<int, int> top_left = std::make_pair(static_cast<int>(bbox_tl_0(0)), static_cast<int>(bbox_tl_0(1)));
            std::pair<int, int> bottom_right = std::make_pair(static_cast<int>(bbox_br_0(0)), static_cast<int>(bbox_br_0(1)));
            // Giving the initial bounding box of the object from outside
             pc_icub = std::unique_ptr<iCubPointCloud>(new iCubPointCloud(port_prefix,
                                                                          "object-tracking",
                                                                          "sfm_config.ini",
                                                                          object_mesh_path_obj,
                                                                          rf.findPath("shader/"),
                                                                          "left",
                                                                          std::make_pair(top_left, bottom_right),
                                                                          std::move(pc_prediction),
                                                                          noise_covariance_diagonal,
                                                                          icub_pc_exog_data));
        }
        else
        {
            // Using OPC/IOL to take the initial bounding box of the object
            pc_icub = std::unique_ptr<iCubPointCloud>(new iCubPointCloud(port_prefix,
                                                                         "object-tracking",
                                                                         "sfm_config.ini",
                                                                         iol_object_name,
                                                                         object_mesh_path_obj,
                                                                         rf.findPath("shader/"),
                                                                         "left",
                                                                         std::move(pc_prediction),
                                                                         noise_covariance_diagonal,
                                                                         icub_pc_exog_data));
        }

        measurement_model = std::move(pc_icub);
    }

    if (enable_log)
        measurement_model->enable_log(log_path, "object-tracking");

    /**
     * Filter construction.
     */

    /**
     * StateModel
     */
    std::unique_ptr<LinearStateModel> kinematic_model = std::unique_ptr<KinematicModel>(
        new KinematicModel(sample_time,
                           kin_psd_acc(0), kin_psd_acc(1), kin_psd_acc(2),
                           kin_psd_euler(0), kin_psd_euler(1), kin_psd_euler(2)));

    std::size_t dim_linear;
    std::size_t dim_circular;
    std::tie(dim_linear, dim_circular) = kinematic_model->getOutputSize();
    std::size_t state_size = dim_linear + dim_circular;

    /**
     * Initial condition.
     */
    Gaussian initial_state(dim_linear, dim_circular);
    VectorXd initial_mean(12);
    initial_mean.head<3>() = x_0;
    initial_mean.segment<3>(3) = v_0;
    initial_mean.segment<3>(6) = euler_dot_0;
    initial_mean.tail<3>() = euler_0;

    VectorXd initial_covariance(12);
    initial_covariance.head<3>() = cov_x_0;
    initial_covariance.segment<3>(3) = cov_v_0;
    initial_covariance.segment<3>(6) = cov_eul_dot_0;
    initial_covariance.tail<3>() = cov_eul_0;

    initial_state.mean() =  initial_mean;
    initial_state.covariance() = initial_covariance.asDiagonal();

    /**
     * Prediction step.
     */
    std::unique_ptr<GaussianPrediction> prediction =
        std::unique_ptr<KFPrediction>(new KFPrediction(std::move(kinematic_model)));

    /**
     * Correction step.
     */
    std::size_t measurement_sub_size = 3; // i.e. a measurement is made of 3 * N points
                                          // where N is the number of points belonging to the point cloud

    std::unique_ptr<Correction> correction =
        std::unique_ptr<Correction>(new Correction(std::move(measurement_model),
                                                   state_size,
                                                   ut_alpha, ut_beta, ut_kappa,
                                                   measurement_sub_size));
    /**
     * Filter.
     */
    std::cout << "Initializing filter..." << std::flush;

    std::unique_ptr<GaussianFilter_> filter;
    if (mode == "simulation")
    {
        filter = std::move(std::unique_ptr<SimulatedFilter>(
                               new SimulatedFilter(initial_state,
                                                   std::move(prediction),
                                                   std::move(correction),
                                                   sim_duration / sim_sample_time)));
    }

    std::cout << "done." << std::endl;

    if (enable_log)
        filter->enable_log(log_path, "object-tracking");

    std::cout << "Booting filter..." << std::flush;

    filter->boot();

    std::cout << "done." << std::endl;

    if (mode == "simulation")
    {
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        filter->run();

        std::cout << "Running filter..." << std::endl;

        if (!filter->wait())
            return EXIT_FAILURE;

        std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();

        std::cout << "Done in "
                  << std::chrono::duration_cast<std::chrono::seconds>(end - start).count()
                  << "s"
                  << std::endl;
    }
    else
    {
        std::cout << "Running filter..." << std::endl;

        if (!filter->wait())
            return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
