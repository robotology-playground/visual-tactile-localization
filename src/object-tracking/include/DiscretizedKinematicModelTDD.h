#ifndef DISCRETIZEDKINEMATICMODELTDD_H
#define DISCRETIZEDKINEMATICMODELTDD_H

#include <BayesFilters/LinearStateModel.h>

#include <Eigen/Dense>

#include <chrono>
#include <memory>

class DiscretizedKinematicModelTDD : public bfl::LinearStateModel
{
public:
    DiscretizedKinematicModelTDD
    (
        const double sigma_x,
        const double sigma_y,
        const double sigma_z,
        const double sigma_yaw,
        const double sigma_pitch,
        const double sigma_roll,
        const double gain,
        const int max_iterations
    );

    DiscretizedKinematicModelTDD
    (
        const double sigma_x,
        const double sigma_y,
        const double sigma_z,
        const double sigma_yaw,
        const double sigma_pitch,
        const double sigma_roll,
        const double gain,
        const double max_seconds
    );

    virtual ~DiscretizedKinematicModelTDD();

    Eigen::MatrixXd getStateTransitionMatrix() override;

    bool setProperty(const std::string& property) override;

    Eigen::MatrixXd getNoiseCovarianceMatrix();

    std::pair<std::size_t, std::size_t> getOutputSize() const override;

protected:
    DiscretizedKinematicModelTDD
    (
        const double sigma_x,
        const double sigma_y,
        const double sigma_z,
        const double sigma_phi,
        const double sigma_theta,
        const double sigma_psi,
        const double gain
    );

    void evaluateStateTransitionMatrix(const double T);

    void evaluateNoiseCovarianceMatrix(const double T);

    /**
     * State transition matrix.
     */
    Eigen::MatrixXd F_;

    /**
     * Noise covariance matrix.
     */
    Eigen::MatrixXd Q_;

    /**
     * Squared power spectral densities
     */
    Eigen::VectorXd sigma_position_;

    Eigen::VectorXd sigma_orientation_;

    std::chrono::high_resolution_clock::time_point last_time_;

    bool last_time_set_ = false;

    struct ImplData;

    std::unique_ptr<ImplData> pImpl_;
};

#endif /* DISCRETIZEDKINEMATICMODELTDD_H */