
#include "include/AnglePlugin.hh"


using namespace gazebo;

GZ_REGISTER_WORLD_PLUGIN(AnglePlugin)

void AnglePlugin::Load(physics::WorldPtr _world, sdf::ElementPtr _sdf)
{

    if (_sdf->HasElement("target"))
    {
        sdf::ElementPtr elem = _sdf->GetElement("target");
        this->model_target_name = elem->Get<std::string>();
    }

    // Setup UDP Client

    // Create socket file descriptor
    if ((this->sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        gzerr << "Socket Creation Failed" << std::endl;

    }

    this->servaddr.sin_family = AF_INET;
    this->servaddr.sin_port = htons(this->server_port);
    this->servaddr.sin_addr.s_addr = INADDR_ANY; // Connect to this host itself

    // Get Gazebo World Pointer
    this->world = _world;
    // Get Model Pointers for the target and drone
    this->model_drone = this->world->ModelByName("iris");
    this->model_target = this->world->ModelByName(this->model_target_name);

    // Log data to CSV file
    myFile.open("data.csv");





    this->updateConnection = event::Events::ConnectWorldUpdateBegin(
        std::bind(&AnglePlugin::OnUpdate, this, std::placeholders::_1));
}

void AnglePlugin::OnUpdate(const common::UpdateInfo &_info)
{
    // Declare timer variables
    if (this->setup_bool == false)
    {
        this->current_time = 0.0;
        this->start_time = _info.simTime.Double(); // In seconds
        this->setup_bool = true;
    }

    // Get current time
    this->current_time = _info.simTime.Double();

    // Update every period
    if (this->current_time - this->start_time >= this->period)
    {
        // Get position vectors of both objects
        const ignition::math::Pose3d drone_pose = this->model_drone->WorldPose();
        const ignition::math::Pose3d target_pose = this->model_target->WorldPose();

        // Find relative distance (In gazebo world frame) in meters
        const float x_rel = target_pose.Pos().X() - drone_pose.Pos().X();
        const float y_rel = target_pose.Pos().Y() - drone_pose.Pos().Y();
        //gzdbg << "Relative Distance X: " << x_rel << "Relative Distance Y: " << y_rel << std::endl;
        
        // Get Height
        const float height = drone_pose.Pos().Z();
        const ignition::math::Vector3f rel_pos = ignition::math::Vector3f(
            x_rel, y_rel, height
        );

        // Construct rotation matrix (From Gazebo World frame to Vehicle body frame)
        const ignition::math::Vector3d drone_angle = drone_pose.Rot().Euler(); // Roll, Pitch, yaw
        const float sin_roll_align = sinf(drone_angle.X());
        const float cos_roll_align = cosf(drone_angle.X());
        const float sin_pitch_align = sinf(drone_angle.Y());
        const float cos_pitch_align = cosf(drone_angle.Y());
        const float sin_yaw_align = sinf(drone_angle.Z());
        const float cos_yaw_align = cosf(drone_angle.Z());

        const ignition::math::Matrix3f Rx = ignition::math::Matrix3f(
            1, 0, 0,
            0, cos_roll_align, -sin_roll_align,
            0, sin_roll_align, cos_roll_align
        );
        const ignition::math::Matrix3f Ry = ignition::math::Matrix3f(
            cos_pitch_align, 0, sin_pitch_align,
            0, 1, 0,
            -sin_pitch_align, 0, cos_pitch_align
        );
        const ignition::math::Matrix3f Rz = ignition::math::Matrix3f(
            cos_yaw_align, -sin_yaw_align, 0,
            sin_yaw_align, cos_yaw_align, 0,
            0, 0, 1
        );
        // Rotation from gazebo world frame to gazebo body frame
        const ignition::math::Matrix3f rot = Rx * Ry * Rz;
        ignition::math::Vector3f corrected_pos = rot * rel_pos;
        // Normalize the vector
        // http://mathworld.wolfram.com/NormalizedVector.html
        //corrected_pos = corrected_pos.Normalized();
        corrected_pos = corrected_pos / height;
        // Gazebo has NWU (FLU) and Ardupilot has (NED, FRD)
        // Get angles
        //float angle_x = atan2(corrected_pos.X(),  corrected_pos.Z()); // In Radians
        //float angle_y = atan2(corrected_pos.Y(), corrected_pos.Z());

        // Publish Data
        DataPacket packet;
       
        packet.timestamp = static_cast<uint64_t>
            (1.0e3 * this->current_time);
        packet.num_targets = static_cast<uint16_t>(1);
        packet.pos_x = -corrected_pos.Y();
        packet.pos_y = -corrected_pos.X();
        packet.size_x = static_cast<float>(1);
        packet.size_y = static_cast<float>(1);

        // sendto(this->sock_fd, 
        // reinterpret_cast<raw_type *>(&packet),
        // sizeof(packet), 0,
        // (struct sockaddr *)&this->servaddr, sizeof(this->servaddr) 
        // );


        // Log data to CSV file
        myFile << -corrected_pos.Y() << "," << corrected_pos.X() << std::endl;
        //myFile << packet.timestamp << std::endl;

        // Save start time
        this->start_time = this->current_time;
    }

}
