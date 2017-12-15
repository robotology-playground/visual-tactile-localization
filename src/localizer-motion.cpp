/******************************************************************************
 *                                                                            *
 * Copyright (C) 2017 Fondazione Istituto Italiano di Tecnologia (IIT)        *
 * All Rights Reserved.                                                       *
 *                                                                            *
 ******************************************************************************/

/**
 * @author: Nicola Piga <nicolapiga@gmail.com>
 */

// std
#include <fstream>
#include <string>

// yarp
#include <yarp/os/all.h>
#include <yarp/math/Math.h>

// CGAL
#include <CGAL/IO/Polyhedron_iostream.h>

//
#include "headers/localizer-motion.h"
#include "headers/unscentedParticleFilter.h"
#include "headers/geometryCGAL.h"

using namespace yarp::math;

bool LocalizerMotion::loadParameters(const yarp::os::ResourceFinder &rf)
{
    // load the fixed number of contact points per time step
    // for estimation during static phase
    yarp::os::Value n_contacts_value = rf.find("numContacts");
    if (rf.find("numContacts").isNull())
    {
        yError() << "number of contact points not provided.";
        return false;
    }
    if (!n_contacts_value.isInt())
    {
        yError() << "invalid number of contact points provided.";
        return false;
    }
    n_contacts = n_contacts_value.asInt();
    yInfo() << "Localizer: number of contact points per step:"
	    << n_contacts;

    // load the file name of the object model
    model_file_name = rf.find("modelFile").asString();
    if (rf.find("modelFile").isNull())
    {
        yError() << "object model file not provided. Check your configuration.";
        return false;
    }

    // load the file name of the auxiliary point cloud 1
    aux_cloud_1_file_name = rf.find("auxCloud1File").asString();
    if (rf.find("auxCloud1File").isNull())
    {
        yError() << "Auxiliary point cloud 1 file not provided. Check your configuration.";
        return false;
    }

    // load the file name of the auxiliary point cloud 2
    aux_cloud_2_file_name = rf.find("auxCloud2File").asString();
    if (rf.find("auxCloud2File").isNull())
    {
        yError() << "Auxiliary point cloud 2 file not provided. Check your configuration.";
        return false;
    }

    return true;
}

bool LocalizerMotion::saveModel(const Polyhedron &model,
			  const std::string &file_name)
{    
    // get the output path if specified
    std::string outputPath;    
    yarp::os::Value path_value = rf->find("modelOutputPath");
    if (rf->find("modelOutputPath").isNull())
	outputPath = "../../outputs/";
    else
	outputPath = path_value.asString();

    // file name
    std::string outputFileName = outputPath + file_name;

    // save the model
    std::ofstream fout(outputFileName.c_str());
    if(fout.is_open())
    	fout << model;
    else
    {
	fout.close();
	
    	yError() << "problem opening transformed model output file!";
	return false;
    }
    
    fout.close();

    return true;
}

bool LocalizerMotion::saveResults(const std::vector<Results> &results)
{
    // get the output path if specified
    std::string outputPath;    
    yarp::os::Value path_value = rf->find("resultsOutputPath");
    if (rf->find("resultsOutputPath").isNull())
	outputPath = "../../outputs/";
    else
	outputPath = path_value.asString();

    // file name
    std::string outputFileName = outputPath + "results.csv";

    // save the results
    std::ofstream fout(outputFileName.c_str());
    if(fout.is_open())
    {
	// print the CSV header
	fout << "step;"
	     << "x_real;" << "y_real;" << "z_real;"
	     << "phi_real;" << "theta_real;" << "psi_real;"	    
	     << "x_sol;"   << "y_sol;"     << "z_sol;"
	     << "phi_sol;" << "theta_sol;" << "psi_sol"
	     << std::endl;

	// save data for each trial
	for(size_t i=0; i<results.size(); i++)
	{
	    fout << i <<";"
		 << results[i].real[0] << ";"
		 << results[i].real[1] << ";"
		 << results[i].real[2] << ";"
		 << results[i].real[3] << ";"
		 << results[i].real[4] << ";"
		 << results[i].real[5] << ";"
		 << results[i].estimate[0] << ";"
		 << results[i].estimate[1] << ";"
		 << results[i].estimate[2] << ";"
		 << results[i].estimate[3] << ";"
		 << results[i].estimate[4] << ";"
		 << results[i].estimate[5] << ";"
		 << std::endl;
	}
    }
    else
    {
	fout.close();
	
    	yError() << "problem opening transformed model output file!";
	return false;
    }
    
    fout.close();

    return true;
}

bool LocalizerMotion::saveMeas(const std::vector<Measure> &meas, const std::string &filename)
{
    // get the output path if specified
    std::string outputPath;    
    yarp::os::Value path_value = rf->find("measOutputPath");
    if (rf->find("measOutputPath").isNull())
	outputPath = "../../outputs/";
    else
	outputPath = path_value.asString();

    // file name
    std::string outputFileName = outputPath + filename;

    // save the results
    std::ofstream fout(outputFileName.c_str());
    if(fout.is_open())
    {
	// count the total number of points
	int num_points = 0;
	for(size_t i=0; i<meas.size(); i++)
	{
	    const Measure &m = meas[i];
	    num_points += m.size();
	}
	
	// print the OFF header
	fout << "OFF" << std::endl;
	// this is a vertices only .OFF
	fout << num_points << " 0 0" << std::endl;

	// save all the contact points
	for(size_t i=0; i<meas.size(); i++)
	{
	    const Measure &m = meas[i];
	    for(size_t j=0; j<m.size(); j++)
	    {
		const Point &p = m[j];
		fout << p[0] << " "
		     << p[1] << " "
		     << p[2] << " "
		     << std::endl;
	    }
	}	    
    }
    else
    {
	fout.close();
	
    	yError() << "problem opening meas output file!";
	return false;
    }
    
    fout.close();

    return true;
}

void LocalizerMotion::saveLocalizationStep(const yarp::sig::Vector &real_pose,
					   const std::vector<Measure> &meas)
{
    // extract MAP estimate
    yarp::sig::Vector est = upf->getEstimate();

    // save real pose
    Polyhedron real_model;
    upf->transformObject(real_pose, real_model);
    saveModel(real_model, "realPose" + std::to_string(n_steps) + ".off");

    // save estimated pose
    Polyhedron est_model;
    upf->transformObject(est, est_model);
    saveModel(est_model, "estPose" + std::to_string(n_steps) + ".off");

    // save measurements
    saveMeas(meas, "meas" + std::to_string(n_steps) + ".off");

    // save results
    Results result;
    result.real = real_pose;
    result.estimate = est;
    results.push_back(result);
}

bool LocalizerMotion::performLocalization(int &current_phase)
{
    // get current phase	    
    LocalizationPhase &lp = phases[current_phase];
    
    // step and check if trajectory is ended
    bool traj_end = lp.mg->step();
    if(traj_end)
    {
	// increment the current_phase
	current_phase++;
	
	return true;
    }

    // get pose from generator    
    lp.mg->getMotion(cur_pos,
		  cur_vel,
		  cur_ref_vel,
		  cur_yaw);

    // get measurements from fake point cloud
    yarp::sig::Vector pose(6, 0.0);
    std::vector<Point> meas;
    pose.setSubvector(0, cur_pos);
    pose[3] = cur_yaw;
    lp.pc->setPose(pose);
    if (lp.type == LocalizationType::Static)
	lp.pc->samplePointCloud(meas, lp.num_points);
    else if (lp.type == LocalizationType::Motion)
	lp.pc->getPointCloud(meas);

    // set real pose
    upf->setRealPose(pose);

    // perform localization
    if(lp.type == LocalizationType::Static)
    {	
	for(size_t k=0; k+n_contacts-1< meas.size(); k+=n_contacts)	
	{
	    // feed measurements in groups of n_contacts contact points
	
	    // update num of steps
	    n_steps++;
	    yInfo() << "[Static] Step #" << n_steps;

	    // simulate n_contacts contact points per time step
	    Measure m;
	    std::vector<Measure> one_meas;	
	    for(size_t j=0; j<n_contacts; j++)
		m.push_back(meas[k+j]);
	    one_meas.push_back(m);
	
	    // set zero inputs
	    yarp::sig::Vector zero_in(3, 0.0);
	    upf->setNewInput(zero_in);

	    // set new measure
	    upf->setNewMeasure(m);

	    // step
	    upf->step();

	    // save all data
	    saveLocalizationStep(pose, one_meas);
	}
    }	
    else if(lp.type == LocalizationType::Motion)
    {
	n_steps++;
	yInfo() << "[Motion] Step #" << n_steps;
	
	// feed measurements
	std::vector<Measure> one_meas;
	Measure m;
	for(size_t k=0; k<meas.size(); k++)
	    m.push_back(meas[k]);
	one_meas.push_back(m);

	// set real pose
	upf->setRealPose(pose);

	// set inputs
	upf->setNewInput(prev_ref_vel * getPeriod());
	// update velocity
	prev_vel = cur_vel;
	prev_ref_vel = cur_ref_vel;

	// set new measure
	upf->setNewMeasure(m);

	// step
	upf->step();

	// save all data
	saveLocalizationStep(pose, one_meas);
    }

    return false;
}

void LocalizerMotion::configureLocPhase(const int &current_phase)
{
    // get current phase	    
    LocalizationPhase &cur_phase = phases[current_phase];

    // check if initialization
    // has been done before
    if(cur_phase.initialized)
	return;
    
    // configure phase
    if(current_phase >= 1)
    {
	// set initial conditions equal to 
	// the final conditions of the previous step
	LocalizationPhase &prev_phase = phases[current_phase-1];
	cur_phase.ref_pos_0 = prev_phase.ref_pos_f;
	cur_phase.yaw_0 = prev_phase.yaw_f;

	if (cur_phase.holdDisplFromPrevious)
	    // copy displacement from reference point to
	    // the center of the object
	    cur_phase.displ = prev_phase.displ;
	else
	{
	    // the displacement from the reference point to
	    // the center is changed, then cur_phase.ref_pos_0
	    // should be updated accordingly
	    yarp::sig::Vector center_to_old_disp = -1 * (prev_phase.displ);
	    yarp::sig::Vector center_to_new_disp = -1 * (cur_phase.displ);

	    // evaluate rotation matrix using the final yaw of the previous
	    // phase
	    yarp::sig::Vector axis_angle(4, 0.0);
	    yarp::sig::Matrix yaw_rot;

	    axis_angle[2] = 1.0;
	    axis_angle[3] = prev_phase.yaw_f;
	    yaw_rot = yarp::math::axis2dcm(axis_angle).submatrix(0, 2,
								0, 2);

	    // update cur_phase.ref_pos_0
	    cur_phase.ref_pos_0 = prev_phase.ref_pos_0 +
		yaw_rot * (center_to_new_disp - center_to_old_disp);
	}
    }
    if(cur_phase.type == LocalizationType::Static)
    {
	// in case of static localization final and initial
	// conditions are the same
	cur_phase.ref_pos_f = cur_phase.ref_pos_0;
	cur_phase.yaw_f = cur_phase.yaw_0;
    }

    // configure motion generator
    MotionGenerator &mg = *(cur_phase.mg);
    mg.setInitialRefPosition(cur_phase.ref_pos_0);
    mg.setInitialYaw(cur_phase.yaw_0);
    mg.setDuration(cur_phase.duration);
    mg.setPeriod(cur_phase.step_time);	    
    mg.setDisplToCenter(cur_phase.displ);
    if(cur_phase.type == LocalizationType::Motion)
    {
	// in case of localization in motion
	// the trajectory generator has to be initialized
	PolynomialMotionGenerator *pmg;
	pmg = (PolynomialMotionGenerator*)(&mg);
	// configure final conditions of trajectory
	pmg->setFinalRefPosition(cur_phase.ref_pos_f);
	pmg->setFinalYaw(cur_phase.yaw_f);
	pmg->initTrajectory();		
    }
    mg.reset();	    

    // set system noise covariance matrix	    
    yarp::sig::Vector Q(6, 0.0);
    if(cur_phase.type == LocalizationType::Static)
    {
	// settings for the static scenario
	Q.setSubvector(0, yarp::sig::Vector(3, 0.0001));
	Q.setSubvector(3, yarp::sig::Vector(3, 0.01));		
    }
    else if(cur_phase.type == LocalizationType::Motion)
    {
	// settings for the motion scenario
	// the artificial noise has a greater covariance
	// in the yaw component
	Q.setSubvector(0, yarp::sig::Vector(5, 0.000001));
	Q[2] = 1.0e-10;
	Q[5] = 0.01;
    }
    upf->setQ(Q);	    

    cur_phase.initialized = true;
}

bool LocalizerMotion::configure(yarp::os::ResourceFinder &rf)
{
    // save reference to resource finder
    this->rf = &rf;
    
    // load the parameters
    if(!loadParameters(rf))
    	return false;

    // initialize the fakePointCloud engines
    pc_whole.loadObjectModel(model_file_name);
    pc_contacts_1.loadObjectModel(aux_cloud_1_file_name);
    pc_contacts_2.loadObjectModel(aux_cloud_2_file_name);    

    // instantiate the UPF
    upf = new UnscentedParticleFilter;

    // configure and init the UPF
    if(!upf->configure(rf))
	return false;
    upf->init();

    //set the initial state
    state = State::init;

    // reset the number of steps
    n_steps = 0;

    return true;
}

double LocalizerMotion::getPeriod()
{
    return 0.01;
}

bool LocalizerMotion::updateModule()
{
    if(state == State::init)
    {
	// setup the localization experiment

	/*
	 * Static phase #1
	 */
	LocalizationPhase static1(LocalizationType::Static);

	// set displacement from ref point to center
    	static1.displ[0] = 0.0282;
    	static1.displ[1] = -0.0090;
    	static1.displ[2] = -0.0006;
	
    	// set initial conditions
    	static1.ref_pos_0[0] = 0.1;
    	static1.ref_pos_0[1] = 0.1;
    	static1.yaw_0 = 0.0;

	// set number of iterations
	static1.duration = 2;
	static1.step_time = 1;

	// set motion generator
	static1.mg = &static_mg;

	// set fake point cloud
	static1.pc = &pc_whole;
	static1.num_points = 100;
	/*
	 */

	/*
	 * Motion phase #1
	 */
	LocalizationPhase motion1(LocalizationType::Motion,
				  true);

	// set final conditions
	motion1.ref_pos_f = static1.ref_pos_0;
	motion1.ref_pos_f[0] = 0.3;
	motion1.yaw_f = static1.yaw_0 - M_PI/8.0;

	// set duration and step time
	motion1.duration = 2.5;
	motion1.step_time = getPeriod();

	// set motion generator
	motion1.mg = &motion_mg;

	// set fake point cloud
	motion1.pc = &pc_contacts_1;
	/*
	 */

	/*
	 * Static phase #2
	 */
	LocalizationPhase static2(LocalizationType::Static,
				  true);
	
	// set number of iterations
	static2.duration = 1;
	static2.step_time = 1;

	// set motion generator
	static2.mg = &static_mg;

	// set fake point cloud
	static2.pc = &pc_whole;
	static2.num_points = 100;
	/*
	 */

	/*
	 * Motion phase #2
	 */
	LocalizationPhase motion2(LocalizationType::Motion);

	// change reference point
	motion2.displ[0] = -0.0282;
    	motion2.displ[1] = -0.0090;
    	motion2.displ[2] = -0.0006;

	// set final conditions
	motion2.ref_pos_f[0] = 0.3521;
	motion2.ref_pos_f[1] = 0.3784;
	motion2.yaw_f = static1.yaw_0 - M_PI/4.0;

	// set duration and step time
	motion2.duration = 2.5;
	motion2.step_time = getPeriod();

	// set motion generator
	motion2.mg = &motion_mg;

	// set fake point cloud
	motion2.pc = &pc_contacts_2;

	/*
	 * Static phase #3
	 */
	LocalizationPhase static3(LocalizationType::Static,
				  true);
	// set number of iterations
	static3.duration = 1;
	static3.step_time = 1;

	// set motion generator
	static3.mg = &static_mg;

	// set fake point cloud
	static3.pc = &pc_whole;
	static3.num_points = 100;	

	/*
	 * add phases to the list
	 */
	phases.push_back(static1);
	phases.push_back(motion1);
	phases.push_back(static2);
	phases.push_back(motion2);
	phases.push_back(static3);	

	// switch state
	state = State::run;
    }
    else if (state == State::run)
    {
	// check if the phases are all done
	if (current_phase == phases.size())
	{
	    state = State::end;

	    return true;
	}
	
	// configure localization phase
	configureLocPhase(current_phase);

	// perform localization
	performLocalization(current_phase);
    }
    else if (state == State::end)
    {
	// save results
	saveResults(results);	

	// stop module
	return false;
    }

    return true;
}

bool LocalizerMotion::close()
{
    // delete the UPF instance
    delete upf;
}