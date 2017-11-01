#ifndef FWDKINMODEL_H
#define FWDKINMODEL_H

#include <BayesFilters/ExogenousModel.h>
#include <iCub/iKin/iKinFwd.h>
#include <yarp/dev/PolyDriver.h>
#include <yarp/dev/IEncoders.h>
#include <yarp/os/ConstString.h>
#include <yarp/sig/Vector.h>


class FwdKinModel : public bfl::ExogenousModel
{
public:
    FwdKinModel() noexcept;

    ~FwdKinModel() noexcept;

    void propagate(const Eigen::Ref<const Eigen::MatrixXf>& cur_state, Eigen::Ref<Eigen::MatrixXf> prop_state) override;

    Eigen::MatrixXf getExogenousMatrix() override;

    bool setProperty(const std::string& property) override;

protected:
    virtual Eigen::VectorXd readPose() = 0;

    bool init_delta_ = false;
    bool setDeltaMotion();

private:
    Eigen::VectorXd prev_ee_pose_    = Eigen::VectorXd::Zero(7);
    Eigen::VectorXd delta_hand_pose_ = Eigen::VectorXd::Zero(7);
    double          delta_angle_     = 0.0;
};

#endif /* FWDKINMODEL_H */
