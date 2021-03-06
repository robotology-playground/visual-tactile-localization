/*
 * Copyright (C) 2019 Istituto Italiano di Tecnologia (IIT)
 *
 * This software may be modified and distributed under the terms of the
 * GPL-2+ license. See the accompanying LICENSE file for details.
 */

#ifndef ARUCOMEASUREMENT_H
#define ARUCOMEASUREMENT_H

#include <opencv2/opencv.hpp>
#include <opencv2/aruco.hpp>

#include <BayesFilters/Data.h>
#include <BayesFilters/LinearMeasurementModel.h>

#include <Eigen/Dense>

#include <GazeController.h>

#include <yarp/os/BufferedPort.h>
#include <yarp/sig/Image.h>
#include <yarp/sig/Vector.h>

#include <unordered_map>


class ArucoMeasurement : public bfl::LinearMeasurementModel
{
public:
    ArucoMeasurement
    (
        const std::string port_prefix,
        const std::string eye_name,
        const Eigen::VectorXi& marker_ids,
        const Eigen::VectorXd& marker_lengths,
        const std::vector<Eigen::VectorXd>& marker_offsets,
        Eigen::Ref<Eigen::MatrixXd> noise_covariance,
        const bool send_image,
        const bool send_aruco_estimate
    );

    virtual ~ArucoMeasurement();

    std::pair<bool, bfl::Data> measure(const bfl::Data& data = bfl::Data()) const override;

    bool freeze() override;

    std::pair<bool, bfl::Data> innovation(const bfl::Data& predicted_measurements, const bfl::Data& measurements) const override;

    Eigen::MatrixXd getMeasurementMatrix() const;

    std::pair<bool, Eigen::MatrixXd> getNoiseCovarianceMatrix() const;

    std::pair<std::size_t, std::size_t> getOutputSize() const override;

    bool setProperty(const std::string& property) override;

protected:
    std::pair<bool, Eigen::Transform<double, 3, Eigen::Affine>> getCameraPose();

    const std::string eye_name_;

    cv::Ptr<cv::aruco::Dictionary> dictionary_;

    cv::Mat cam_intrinsic_;

    cv::Mat cam_distortion_;

    cv::Mat image_with_marker_;

    std::unordered_map<int, int> marker_ids_;

    Eigen::VectorXd marker_lengths_;

    std::vector<Eigen::VectorXd> marker_offsets_;

    double marker_length_;

    /**
     * Gaze controller.
     */
    GazeController gaze_;

    /**
     * Latest Object pose. This pose is used as a measurement.
     */
    Eigen::MatrixXd measurement_;
    bool is_measurement_available_;

    /*
     * Linear measurement matrix
     */
    Eigen::MatrixXd H_;

    /*
     * Noise covariance matrix
     */
    Eigen::MatrixXd R_;

    /**
     * Image input / output.
     */
    yarp::os::BufferedPort<yarp::sig::ImageOf<yarp::sig::PixelRgb>> port_image_in_;

    yarp::os::BufferedPort<yarp::sig::ImageOf<yarp::sig::PixelRgb>> port_image_out_;

    yarp::os::BufferedPort<yarp::sig::Vector> port_aruco_estimate_out_;

    bool send_image_;

    bool send_aruco_estimate_;

    const std::string log_ID_ = "[ARUCOMEASUREMENT]";
};

#endif /* ARUCOMEASUREMENT_H */
