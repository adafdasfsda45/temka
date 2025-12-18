#include "misc.h"
#include <cmath>
#include <iostream>

double SetDoubleValue(uintptr_t self, double value)
{
    Write<double>(self + offsets::Value, value);
    return Read<double>(self + offsets::Value);
}

std::vector<uintptr_t> GetChildren(uintptr_t self)
{
    std::vector<uintptr_t> container;
    if (!self)
        return container;
    auto start = Read<std::uint64_t>(self + offsets::Children);
    if (!start)
        return container;
    auto end = Read<std::uint64_t>(start + offsets::ChildrenEnd);
    if (!end)
        return container;
    for (auto instances = Read<std::uint64_t>(start); instances != end; instances += 16) {
        if (instances > 1099511627776 && instances < 3298534883328) {
            container.emplace_back(Read<uintptr_t>(instances));
        }
    }
    return container;
}

uintptr_t FindFirstChild(uintptr_t self, const std::string& Name)
{
    if (!self || Name.empty())
        return 0;
    std::vector<uintptr_t> children = GetChildren(self);
    for (auto& object : children)
    {
        std::string objName = utils::get_instance_name(object);
        if (objName == Name)
        {
            return object;
        }
    }
    return 0;
}

uintptr_t FindFirstChildOfClass(uintptr_t self, const std::string& Name)
{
    if (!self || Name.empty())
        return 0;
    std::vector<uintptr_t> children = GetChildren(self);
    for (auto& object : children)
    {
        std::string objClass = utils::get_instance_classname(object);
        if (objClass == Name)
        {
            return object;
        }
    }
    return 0;
}

uintptr_t GetLocalPlayer(uintptr_t self)
{
    if (!self)
        return 0;
    return Read<uintptr_t>(self + offsets::LocalPlayer);
}

uintptr_t GetCharacter(uintptr_t self)
{
    if (!self)
        return 0;
    return Read<uintptr_t>(self + offsets::ModelInstance);
}

class Vector3 {
public:
    float x, y, z;

    Vector3() : x(0), y(0), z(0) {}
    Vector3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vector3 operator+(const Vector3& other) const {
        return Vector3(x + other.x, y + other.y, z + other.z);
    }

    Vector3 operator-(const Vector3& other) const {
        return Vector3(x - other.x, y - other.y, z - other.z);
    }

    Vector3 operator*(float scalar) const {
        return Vector3(x * scalar, y * scalar, z * scalar);
    }

    Vector3 operator/(float scalar) const {
        return Vector3(x / scalar, y / scalar, z / scalar);
    }

    float dot(const Vector3& other) const {
        return x * other.x + y * other.y + z * other.z;
    }

    Vector3 cross(const Vector3& other) const {
        return Vector3(
            y * other.z - z * other.y,
            z * other.x - x * other.z,
            x * other.y - y * other.x
        );
    }

    float length() const {
        return std::sqrt(x * x + y * y + z * z);
    }

    Vector3 normalized() const {
        float len = length();
        return (len == 0) ? Vector3() : (*this) / len;
    }

    friend std::ostream& operator<<(std::ostream& os, const Vector3& v) {
        os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
        return os;
    }
};

void CMisc::teleport_to_nearest(matrix viewmatrix)
{
    if (!vars::misc::teleport_to_nearest)
        return;

    POINT cursor;
    if (!GetCursorPos(&cursor))
        return;

    std::vector<uintptr_t> player_instances = utils::get_players(globals::datamodel);

    uintptr_t closest_target = 0;
    float best_distance = FLT_MAX;

    for (auto& player_instance : player_instances)
    {
        if (!player_instance || player_instance == globals::local_player)
            continue;

        auto character = utils::get_model_instance(player_instance);
        if (!character)
            continue;

        uintptr_t targetPrimitive = 0;
        auto headBone = utils::find_first_child(character, "Head");
        if (headBone)
            targetPrimitive = Read<uintptr_t>(headBone + offsets::Primitive);
        if (!targetPrimitive)
        {
            auto rootBone = utils::find_first_child(character, "HumanoidRootPart");
            if (rootBone)
                targetPrimitive = Read<uintptr_t>(rootBone + offsets::Primitive);
        }
        if (!targetPrimitive)
            continue;

        Vector partPos = Read<Vector>(targetPrimitive + offsets::Position);
        Vector2D screenPos;
        if (!utils::world_to_screen(partPos, screenPos, viewmatrix, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)))
            continue;

        float dx = static_cast<float>(cursor.x) - screenPos.x;
        float dy = static_cast<float>(cursor.y) - screenPos.y;
        float distance = sqrtf(dx * dx + dy * dy);

        if (distance < best_distance)
        {
            best_distance = distance;
            closest_target = player_instance;
            globals::teleport_target = player_instance;
        }
    }

    if (closest_target)
    {
        auto character = utils::get_model_instance(closest_target);
        if (!character)
            return;

        uintptr_t chosen_part = utils::find_first_child(character, "Head");
        if (!chosen_part) return;

        static bool xButtonPressed = false;

        if (GetAsyncKeyState(vars::misc::teleport_key) & 0x8000)
        {
            if (!xButtonPressed)
            {
                utils::console_print_color(__FILE__, "CMISC::TELEPORT_TO_NEAREST call");
                utils::teleport_to_part(globals::local_player, chosen_part);
                xButtonPressed = true;
            }
        }
        else
        {
            xButtonPressed = false;
        }
    }
}

void CMisc::noclip()
{
    if (!globals::local_player)
        return;

    if (!vars::misc::noclip_enabled)
        return;

    static bool noclip_toggled = false;
    bool should_noclip = false;

    if (vars::misc::noclip_mode == 0)
    {
        should_noclip = GetAsyncKeyState(vars::misc::noclip_key);
    }

    else if (vars::misc::noclip_mode == 1)
    {
        if (GetAsyncKeyState(vars::misc::noclip_key) & 0x1)
        {
            noclip_toggled = !noclip_toggled;
        }
        should_noclip = noclip_toggled;
    }

    globals::key_info::noclip_active = should_noclip;

    uintptr_t character = utils::get_model_instance(globals::local_player);
    if (!character)
        return;

    std::vector< std::string > targetNames;
    if (utils::find_first_child(character, "Torso"))
    {
        targetNames = { "Head", "HumanoidRootPart", "Torso" };
    }
    else if (utils::find_first_child(character, "UpperTorso"))
    {
        targetNames = { "Head", "HumanoidRootPart", "UpperTorso", "LowerTorso" };
    }
    else
    {
        targetNames = { "Head", "HumanoidRootPart", "Torso" };
    }

    for (const auto& partName : targetNames)
    {
        uintptr_t child_instance = utils::find_first_child(character, partName);
        if (child_instance)
        {
            std::string classname = utils::get_instance_classname(child_instance);
            if (classname == "Part" || classname == "MeshPart")
            {
                uint64_t part_primitive = Read< uintptr_t >(child_instance + offsets::Primitive);
                Write< bool >(part_primitive + offsets::CanCollide, !should_noclip);
            }
        }
    }
}

void CMisc::spinbot()
{
    if (!globals::local_player)
        return;

    if (!vars::misc::spinbot_enabled)
        return;

    uintptr_t character = utils::get_model_instance(globals::local_player);
    if (!character)
        return;

    uintptr_t humanoidRootPart = utils::find_first_child(character, "HumanoidRootPart");
    if (!humanoidRootPart)
        return;

    uintptr_t primitive = Read<uintptr_t>(humanoidRootPart + offsets::Primitive);
    if (!primitive)
        return;

    static float spinAngle = 0.0f;

    Matrix3 spinMatrix;

    if (vars::misc::spinbot_mode == 0)
    {
        spinAngle += 0.3f;
        if (spinAngle > 2 * M_PI)
            spinAngle -= 2 * M_PI;

        float cosAngle = cos(spinAngle);
        float sinAngle = sin(spinAngle);

        spinMatrix.data[0] = cosAngle;    spinMatrix.data[1] = 0.0f;  spinMatrix.data[2] = -sinAngle;
        spinMatrix.data[3] = 0.0f;        spinMatrix.data[4] = 1.0f;  spinMatrix.data[5] = 0.0f;
        spinMatrix.data[6] = sinAngle;    spinMatrix.data[7] = 0.0f;  spinMatrix.data[8] = cosAngle;
        Write<Matrix3>(primitive + offsets::CFrame, spinMatrix);

    }
    else if (vars::misc::spinbot_mode == 1)
    {
        static int jitterDirection = 1;
        static int jitterCount = 0;

        jitterCount++;
        if (jitterCount > 5)
        {
            jitterDirection = -jitterDirection;
            jitterCount = 0;
        }

        float jitterAngle = jitterDirection * (M_PI / 2);
        float cosAngle = cos(jitterAngle);
        float sinAngle = sin(jitterAngle);

        spinMatrix.data[0] = cosAngle;    spinMatrix.data[1] = 0.0f;  spinMatrix.data[2] = -sinAngle;
        spinMatrix.data[3] = 0.0f;        spinMatrix.data[4] = 1.0f;  spinMatrix.data[5] = 0.0f;
        spinMatrix.data[6] = sinAngle;    spinMatrix.data[7] = 0.0f;  spinMatrix.data[8] = cosAngle;
        Write<Matrix3>(primitive + offsets::CFrame, spinMatrix);
    }
    else if (vars::misc::spinbot_mode == 2)
    {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> distr(0, 5);

        Vector oldvel = Read<Vector>(primitive + offsets::Position);

        Vector newvec(oldvel.x + distr(gen), oldvel.y + distr(gen), oldvel.z + distr(gen));

        Write<Vector>(primitive + offsets::Position, newvec);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        Write<Vector>(primitive + offsets::Position, oldvel);
    }
}

void CMisc::fly()
{
    if (!globals::local_player)
        return;

    if (!vars::misc::fly_enabled)
        return;

    static bool fly_toggled = false;
    bool should_fly = false;

    if (vars::misc::fly_mode == 0)
    {
        should_fly = GetAsyncKeyState(vars::misc::fly_toggle_key);
    }
    else if (vars::misc::fly_mode == 1)
    {
        if (GetAsyncKeyState(vars::misc::fly_toggle_key) & 0x1)
        {
            fly_toggled = !fly_toggled;
        }
        should_fly = fly_toggled;
    }

    if (!should_fly)
        return;

    uintptr_t character = utils::get_model_instance(globals::local_player);
    if (!character)
        return;

    uintptr_t humanoidRootPart = utils::find_first_child(character, "HumanoidRootPart");
    if (!humanoidRootPart)
        return;

    uintptr_t primitive = Read<uintptr_t>(humanoidRootPart + offsets::Primitive);
    if (!primitive)
        return;

    uintptr_t workspace = Read<uintptr_t>(globals::datamodel + offsets::Workspace);
    if (!workspace)
        return;

    uintptr_t Camera = Read<uintptr_t>(workspace + offsets::Camera);
    if (!Camera)
        return;

    Vector camPos = Read<Vector>(Camera + offsets::CameraPos);
    Matrix3 camCFrame = Read<Matrix3>(Camera + offsets::CameraRotation);
    Vector currentPos = Read<Vector>(primitive + offsets::Position);

    Vector lookVector = Vector(-camCFrame.data[2], -camCFrame.data[5], -camCFrame.data[8]);
    Vector rightVector = Vector(camCFrame.data[0], camCFrame.data[3], camCFrame.data[6]);

    Vector moveDirection(0, 0, 0);

    if (vars::misc::fly_method == 0)
    {
        Vector flatLook = lookVector;
        if (flatLook.y != 0)
        {
            flatLook.y = 0;
            if (!flatLook.IsZero())
                flatLook.Normalize();
        }

        if (GetAsyncKeyState('W') & 0x8000)
        {
            moveDirection = moveDirection + lookVector;
        }
        if (GetAsyncKeyState('S') & 0x8000)
        {
            moveDirection = moveDirection - lookVector;
        }
        if (GetAsyncKeyState('A') & 0x8000)
        {
            moveDirection = moveDirection - rightVector;
        }
        if (GetAsyncKeyState('D') & 0x8000)
        {
            moveDirection = moveDirection + rightVector;
        }
        if (GetAsyncKeyState(VK_SPACE) & 0x8000)
        {
            moveDirection.y += 1.0f;
        }

        if (!moveDirection.IsZero())
        {
            moveDirection.Normalize();
            moveDirection = moveDirection * vars::misc::fly_speed;
        }

        Vector newPos = currentPos + moveDirection;
        Write<Vector>(primitive + offsets::Position, newPos);
        Write<bool>(primitive + offsets::CanCollide, false);
        Write<Vector>(primitive + offsets::Velocity, Vector(0, 0, 0));
    }
    else if (vars::misc::fly_method == 1)
    {
        if (GetAsyncKeyState('W') & 0x8000)
        {
            moveDirection = moveDirection + lookVector;
        }
        if (GetAsyncKeyState('S') & 0x8000)
        {
            moveDirection = moveDirection - lookVector;
        }
        if (GetAsyncKeyState('A') & 0x8000)
        {
            moveDirection = moveDirection - rightVector;
        }
        if (GetAsyncKeyState('D') & 0x8000)
        {
            moveDirection = moveDirection + rightVector;
        }
        if (GetAsyncKeyState(VK_SPACE) & 0x8000)
        {
            moveDirection.y += 1.0f;
        }

        if (!moveDirection.IsZero())
        {
            moveDirection.Normalize();
            moveDirection = moveDirection * vars::misc::fly_speed;
        }

        Write<Vector>(primitive + offsets::Velocity, moveDirection * vars::misc::fly_speed);
        Write<bool>(primitive + offsets::CanCollide, false);
    }
}

void CMisc::vehicle_fly()
{
    if (!globals::local_player)
        return;

    if (!vars::misc::vehicle_fly_enabled)
        return;

    static bool fly_toggled = false;
    bool should_fly = false;

    if (vars::misc::vehicle_fly_mode == 0)
    {
        should_fly = (GetAsyncKeyState(vars::misc::vehicle_fly_toggle_key) & 0x8000) != 0;
    }
    else if (vars::misc::vehicle_fly_mode == 1)
    {
        if (GetAsyncKeyState(vars::misc::vehicle_fly_toggle_key) & 0x1)
        {
            fly_toggled = !fly_toggled;
        }
        should_fly = fly_toggled;
    }

    if (!should_fly)
        return;

    uintptr_t workspace = Read<uintptr_t>(globals::datamodel + offsets::Workspace);
    if (!workspace)
        return;

    // Try to find Vehicles container
    uintptr_t vehicles_folder = FindFirstChild(workspace, "Vehicles");
    if (!vehicles_folder)
    {
        // fallback: search for a Folder named Vehicles among children
        std::vector<uintptr_t> topChildren = GetChildren(workspace);
        for (auto &c : topChildren)
        {
            std::string n = utils::get_instance_name(c);
            if (n == "Vehicles") { vehicles_folder = c; break; }
        }
    }
    if (!vehicles_folder)
        return;

    std::vector<uintptr_t> vehicles = GetChildren(vehicles_folder);
    std::string localName = utils::get_instance_name(globals::local_player);

    for (auto &veh : vehicles)
    {
        if (!veh) continue;

        uintptr_t seat = FindFirstChild(veh, "Seat");
        if (!seat) continue;

        // Try to detect occupant by checking for a child named like the player or PlayerName
        bool isMine = false;
        // some games store a StringValue named PlayerName or Owner
        uintptr_t pname = FindFirstChild(seat, "PlayerName");
        if (pname)
        {
            std::string v = utils::get_instance_name(pname);
            if (v == localName) isMine = true;
        }

        // fallback: if seat has a child whose name equals player name
        if (!isMine)
        {
            std::vector<uintptr_t> seatChildren = GetChildren(seat);
            for (auto &sc : seatChildren)
            {
                if (utils::get_instance_name(sc) == localName)
                {
                    isMine = true; break;
                }
            }
        }

        // If still not matched, skip
        if (!isMine) continue;

        uintptr_t engine = FindFirstChild(veh, "Engine");
        if (!engine) continue;

        uintptr_t primitive = Read<uintptr_t>(engine + offsets::Primitive);
        if (!primitive) continue;

        // get camera rotation to compute direction
        uintptr_t Camera = Read<uintptr_t>(workspace + offsets::Camera);
        if (!Camera) return;

        Matrix3 camCFrame = Read<Matrix3>(Camera + offsets::CameraRotation);
        Vector lookVector(-camCFrame.data[2], -camCFrame.data[5], -camCFrame.data[8]);
        Vector rightVector(camCFrame.data[0], camCFrame.data[3], camCFrame.data[6]);
        Vector moveDirection(0, 0, 0);

        if (GetAsyncKeyState('W') & 0x8000) moveDirection = moveDirection + lookVector;
        if (GetAsyncKeyState('S') & 0x8000) moveDirection = moveDirection - lookVector;
        if (GetAsyncKeyState('A') & 0x8000) moveDirection = moveDirection - rightVector;
        if (GetAsyncKeyState('D') & 0x8000) moveDirection = moveDirection + rightVector;
        if (GetAsyncKeyState(VK_SPACE) & 0x8000) moveDirection.y += 1.0f;
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) moveDirection.y -= 1.0f;

        // normalize and scale
        // provide a simple IsZero/Normalize interface via length check
        float len = std::sqrt(moveDirection.x * moveDirection.x + moveDirection.y * moveDirection.y + moveDirection.z * moveDirection.z);
        if (len > 0.0001f)
        {
            moveDirection.x /= len; moveDirection.y /= len; moveDirection.z /= len;
            moveDirection.x *= vars::misc::vehicle_fly_speed;
            moveDirection.y *= vars::misc::vehicle_fly_speed;
            moveDirection.z *= vars::misc::vehicle_fly_speed;
        }

        Write<Vector>(primitive + offsets::Velocity, moveDirection);
        // apply one vehicle only (the player's vehicle)
        break;
    }
}

void CMisc::speed_hack()
{
    static bool speed_toggled = false;
    static bool was_enabled = false;

    uintptr_t character = utils::get_model_instance(globals::local_player);
    if (!character)
        return;

    uintptr_t humanoid = utils::find_first_child_byclass(character, "Humanoid");
    if (!humanoid)
        return;

    uintptr_t humanoidRootPart = utils::find_first_child(character, "HumanoidRootPart");
    if (!humanoidRootPart)
        return;

    uintptr_t primitive = Read<uintptr_t>(humanoidRootPart + offsets::Primitive);
    if (!primitive)
        return;

    float baseSpeed = 16.0f;

    bool should_speed = false;
    if (vars::misc::speed_hack_keybind_mode == 0)
    {
        should_speed = (GetAsyncKeyState(vars::misc::speed_hack_toggle_key) & 0x8000);
    }
    else if (vars::misc::speed_hack_keybind_mode == 1)
    {
        if (GetAsyncKeyState(vars::misc::speed_hack_toggle_key) & 0x1)
        {
            speed_toggled = !speed_toggled;
        }
        should_speed = speed_toggled;
    }

    if (vars::misc::speed_hack_enabled && should_speed)
    {
        if (vars::misc::speed_hack_mode == 0)
        {
            float speedMultiplier = vars::misc::speed_multiplier;
            Write<float>(humanoid + offsets::WalkSpeed, speedMultiplier);
            Write<float>(humanoid + offsets::WalkSpeedCheck, speedMultiplier);
            was_enabled = true;
        }
        else if (vars::misc::speed_hack_mode == 1)
        {
            uintptr_t workspace = Read<uintptr_t>(globals::datamodel + offsets::Workspace);
            if (!workspace)
                return;

            uintptr_t Camera = Read<uintptr_t>(workspace + offsets::Camera);
            if (!Camera)
                return;

            Matrix3 camCFrame = Read<Matrix3>(Camera + offsets::CameraRotation);

            Vector lookVector(-camCFrame.data[2], -camCFrame.data[5], -camCFrame.data[8]);
            Vector rightVector(camCFrame.data[0], camCFrame.data[3], camCFrame.data[6]);
            Vector moveDirection(0, 0, 0);

            if (GetAsyncKeyState('W') & 0x8000)
                moveDirection += lookVector;
            if (GetAsyncKeyState('S') & 0x8000)
                moveDirection -= lookVector;
            if (GetAsyncKeyState('A') & 0x8000)
                moveDirection -= rightVector;
            if (GetAsyncKeyState('D') & 0x8000)
                moveDirection += rightVector;

            moveDirection.y = 0.0f;

            if (!moveDirection.IsZero())
            {
                moveDirection.Normalize();
                float speed = vars::misc::speed_multiplier / 10.0f;
                Vector currentPos = Read<Vector>(primitive + offsets::Position);
                Vector targetPos = currentPos + moveDirection * speed;
                float lerpAlpha = 0.2f;
                Vector lerpedPos = currentPos + (targetPos - currentPos) * lerpAlpha;
                Write<Vector>(primitive + offsets::Position, lerpedPos);
            }
        }
    }
    else if (was_enabled)
    {
        Write<float>(humanoid + offsets::WalkSpeed, baseSpeed);
        Write<float>(humanoid + offsets::WalkSpeedCheck, baseSpeed);
        was_enabled = false;
    }
}


//void CMisc::instant_proximity_prompt()
//{
//    if (!globals::local_player || !vars::misc::instant_prompt_enabled)
//        return;
//
//    uintptr_t workspace = read<uintptr_t>(globals::datamodel + offsets::Workspace);
//    if (!workspace)
//        return;
//
//    auto process_instance = [](uintptr_t instance, auto& self) -> void {
//        std::string className = utils::get_instance_classname(instance);
//        if (className == "ProximityPrompt")
//        {
//            write<float>(instance + offsets::ProximityPromptHoldDuraction, 0.0f);
//        }
//
//        std::vector<uintptr_t> children = utils::children(instance);
//        for (auto& child : children)
//        {
//            self(child, self);
//        }
//        };
//
//    process_instance(workspace, process_instance);
//}

void CMisc::jump_power()
{
    static bool was_enabled = false;

    uintptr_t character = utils::get_model_instance(globals::local_player);
    if (!character)
        return;

    uintptr_t humanoid = utils::find_first_child_byclass(character, "Humanoid");
    if (!humanoid)
        return;

    float baseJumpPower = 20.0f;

    if (vars::misc::jump_power_enabled)
    {
        float jumpPower = vars::misc::jump_power_value;
        Write<float>(humanoid + offsets::JumpPower, jumpPower);
        was_enabled = true;
    }
    else if (was_enabled)
    {
        Write<float>(humanoid + offsets::JumpPower, baseJumpPower);
        was_enabled = false;
    }
}

void CMisc::infinite_jump()
{
    if (!globals::local_player) return;
    if (!vars::misc::endless_jump) return;

    uintptr_t character = utils::get_model_instance(globals::local_player);
    if (!character) return;

    // try to get HumanoidRootPart primitive to set upward velocity
    auto root = utils::find_first_child(character, "HumanoidRootPart");
    if (!root) return;
    uintptr_t prim = Read<uintptr_t>(root + offsets::Primitive);
    if (!prim) return;

    bool space = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    static bool prev_space = false;

    // read current velocity and desired jump power
    Vector vel = Read<Vector>(prim + offsets::Velocity);
    float desired = vars::misc::jump_power_enabled ? vars::misc::jump_power_value : 50.0f;

    // Manage primitive gravity modification so upward velocity writes persist.
    static bool gravity_modified = false;
    static float original_primitive_gravity = 196.2f;

    if (!space) {
        // restore gravity if we modified it earlier
        if (gravity_modified) {
            try { Write<float>(prim + offsets::PrimitiveGravity, original_primitive_gravity); }
            catch (...) {}
            gravity_modified = false;
        }
        return;
    }

    // try to find humanoid to also set JumpPower/AutoJump
    auto humanoid = utils::find_first_child_byclass(character, "Humanoid");
    if (humanoid) {
        // set jump power to desired to strengthen effect
        Write<float>(humanoid + offsets::JumpPower, desired);
        // enable AutoJump flag
        Write<bool>(humanoid + offsets::AutoJumpEnabled, true);
        // also try writing known child Value names that could trigger jump
        const char* jumpNames[] = { "Jump", "JumpRequest", "Jumping", "IsJumping", "AutoJump" };
        for (const char* jn : jumpNames)
        {
            uintptr_t jd = utils::find_first_child(humanoid, jn);
            if (jd)
            {
                // try write boolean value if this child holds one
                Write<bool>(jd + offsets::Value, true);
                if (vars::misc::dump_humanoid_children) {
                    std::string nm = utils::get_instance_name(jd);
                    printf("[DD] Humanoid child: %s -> %p\n", nm.c_str(), (void*)jd);
                }
            }
        }
    }

    // Set vertical velocity directly to desired each frame while space is held.
    // This is more reliable across different physics contexts (no wall required).
    // ensure primitive gravity is reduced so our upward velocity isn't immediately cancelled
    if (!gravity_modified) {
        try { original_primitive_gravity = Read<float>(prim + offsets::PrimitiveGravity); }
        catch (...) { original_primitive_gravity = 196.2f; }
        gravity_modified = true;
    }
    try { Write<float>(prim + offsets::PrimitiveGravity, 0.0f); }
    catch (...) {}

    // set vertical velocity to desired each frame
    vel.y = desired;
    try { Write<Vector>(prim + offsets::Velocity, vel); }
    catch (...) {}

    // small continuous position nudge to help engines that require position changes
    // compute nudge from desired jump power to scale with configured strength  
    float position_nudge = desired * 0.01f; // 1% of desired per frame
    if (position_nudge < 0.06f) position_nudge = 0.06f; // minimum nudge
    try {
        Vector pos = Read<Vector>(prim + offsets::Position);
        pos.y += position_nudge;
        Write<Vector>(prim + offsets::Position, pos);
    }
    catch (...) {}
}

//void CMisc::infinite_jump_smooth()
//{
//    // This function should be called every frame. It will only disable gravity while
//    // the space key is held and restore gravity immediately when released or when
//    // the feature is toggled off.
//    if (!globals::local_player) return;
//
//    uintptr_t character = utils::get_model_instance(globals::local_player);
//    if (!character) return;
//
//    auto root = utils::find_first_child(character, "HumanoidRootPart");
//    if (!root) return;
//
//    uintptr_t prim = Read<uintptr_t>(root + offsets::Primitive);
//    if (!prim) return;
//
//    static bool stored = false;
//    static uintptr_t stored_prim = 0;
//    static float stored_original_gravity = 196.2f;
//
//    bool space = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
//    static bool prev_space = false;
//
//    // If feature disabled but we have stored gravity, restore and clear
//    //if (!vars::misc::endless_jump_smooth) {
//    //    if (stored) {
//    //        try { Write<float>(stored_prim + offsets::PrimitiveGravity, stored_original_gravity); }
//    //        catch (...) {}
//    //        stored = false;
//    //        stored_prim = 0;
//    //    }
//    //    return;
//    //}
//
//    // read current humanoid (optional) and desired jump power
//    uintptr_t humanoid = utils::find_first_child_byclass(character, "Humanoid");
//    float desired_power = vars::misc::jump_power_enabled ? vars::misc::jump_power_value : 50.0f;
//
//    if (space)
//    {
//        // store original gravity for this primitive if not stored or if primitive changed
//        if (!stored || stored_prim != prim) {
//            try { stored_original_gravity = Read<float>(prim + offsets::PrimitiveGravity); }
//            catch (...) { stored_original_gravity = 196.2f; }
//            stored_prim = prim;
//            stored = true;
//        }
//
//        // compute effective gravity factor (0..1) from settings
//        float effective_factor = 0.0f;
//        if (vars::misc::gravity_enabled) {
//            effective_factor = vars::misc::gravity_factor; // direct factor
//        }
//        else if (vars::misc::gravity_modifier_enabled) {
//            effective_factor = vars::misc::gravity_value / 196.2f;
//        }
//        else {
//            effective_factor = 0.0f;
//        }
//
//        // write scaled primitive gravity so gravity still affects ascent according to settings
//        try { Write<float>(prim + offsets::PrimitiveGravity, stored_original_gravity * effective_factor); }
//        catch (...) {}
//
//        // stronger positional lift: apply a direct upward step and set upward velocity
//        Vector cur_pos = Read<Vector>(prim + offsets::Position);
//        // ascend amount scales with (1 - effective_factor): less gravity -> bigger lift
//        float ascend_scale = (1.0f - effective_factor);
//        if (ascend_scale < 0.0f) ascend_scale = 0.0f;
//        // base multiplier, scaled by user speed
//        float ascend_step = vars::misc::endless_jump_smooth_speed * desired_power * 0.05f * (0.5f + ascend_scale);
//        if (ascend_step < 0.05f) ascend_step = 0.05f; // minimum step
//
//        // If space was just pressed (edge) while falling, give a stronger impulse
//        Vector vel_now = Read<Vector>(prim + offsets::Velocity);
//        bool falling = (vel_now.y < -0.5f);
//        if (space && !prev_space && falling) {
//            ascend_step *= 1.6f; // stronger boost on edge press in air
//        }
//
//        cur_pos.y += ascend_step;
//        try { Write<Vector>(prim + offsets::Position, cur_pos); }
//        catch (...) {}
//
//        // set upward velocity to help counter engine corrections; scale with ascend
//        try {
//            Vector vel = Read<Vector>(prim + offsets::Velocity);
//            float desired_vel = desired_power * (0.5f + ascend_scale);
//            if (vel.y < desired_vel) vel.y = desired_vel;
//            Write<Vector>(prim + offsets::Velocity, vel);
//        }
//        catch (...) {}
//
//        // set humanoid JumpPower/AutoJump and trigger JumpRequest flags if available
//        if (humanoid) {
//            try { Write<float>(humanoid + offsets::JumpPower, desired_power); }
//            catch (...) {}
//            try { Write<bool>(humanoid + offsets::AutoJumpEnabled, true); }
//            catch (...) {}
//            const char* jumpNames[] = { "JumpRequest", "Jump", "AutoJump" };
//            for (const char* jn : jumpNames) {
//                uintptr_t jd = utils::find_first_child(humanoid, jn);
//                if (jd) {
//                    try { Write<bool>(jd + offsets::Value, true); }
//                    catch (...) {}
//                }
//            }
//        }
//    } else {
//        // restore gravity if we had modified it
//        if (stored && stored_prim == prim) {
//            try { Write<float>(prim + offsets::PrimitiveGravity, stored_original_gravity); } catch (...) {}
//            stored = false; stored_prim = 0;
//        }
//        // also disable AutoJump if humanoid present
//        if (humanoid) { try { Write<bool>(humanoid + offsets::AutoJumpEnabled, false); } catch (...) {} }
//    }
//
//    // update previous space state for edge detection next frame
//    prev_space = space;
//}

// autonomous gravity control independent of jump functions
void CMisc::gravity_control()
{
    if (!globals::local_player) return;

    uintptr_t character = utils::get_model_instance(globals::local_player);
    if (!character) return;

    auto root = utils::find_first_child(character, "HumanoidRootPart");
    if (!root) return;

    uintptr_t prim = Read<uintptr_t>(root + offsets::Primitive);
    if (!prim) return;
    // Combined gravity control + mid-air jump handler
    static bool attached = false;
    static uintptr_t attached_prim = 0;
    static float saved_gravity = 196.2f;
    static bool prev_space = false;
    static auto last_jump = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    const int jump_cool_ms = 150;

    bool space = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;

    // If the feature flag is off, ensure gravity restored and detach
    if (!vars::misc::gravity_enabled) {
        if (attached) {
            try { Write<float>(attached_prim + offsets::PrimitiveGravity, saved_gravity); }
            catch (...) {}
            attached = false; attached_prim = 0;
        }
        // even if gravity control is off, still allow jump_on_air impulses if enabled
        // handle edge jump via jump_on_air (but do not modify primitive gravity)
        if (vars::misc::jump_on_air) {
            bool trigger_jump = false;
            if (space && !prev_space) trigger_jump = true;
            else if (space && std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - last_jump).count() > jump_cool_ms) trigger_jump = true;
            if (trigger_jump) {
                uintptr_t humanoid = utils::find_first_child_byclass(character, "Humanoid");
                float jumpPower = vars::misc::jump_power_value;
                if (humanoid) { try { jumpPower = Read<float>(humanoid + offsets::JumpPower); } catch(...) {} }
                try { Vector vel = Read<Vector>(prim + offsets::Velocity); vel.y = jumpPower; Write<Vector>(prim + offsets::Velocity, vel); } catch(...) {}
                if (humanoid) { try { Write<bool>(humanoid + offsets::AutoJumpEnabled, true); } catch(...) {} const char* jumpNames[] = { "JumpRequest","Jump","AutoJump" }; for (const char* jn : jumpNames) { uintptr_t jd = utils::find_first_child(humanoid, jn); if (jd) try { Write<bool>(jd + offsets::Value, true); } catch(...) {} } }
                last_jump = std::chrono::steady_clock::now();
            }
        }
        prev_space = space;
        return;
    }

    // On first attach or if primitive changed, save original gravity
    if (!attached || attached_prim != prim) {
        try { saved_gravity = Read<float>(prim + offsets::PrimitiveGravity); }
        catch (...) { saved_gravity = 196.2f; }
        attached_prim = prim; attached = true;
    }

    // Handle edge/hold jump triggers (allows hold-to-repeat via cooldown)
    bool trigger_jump = false;
    if (space && !prev_space) trigger_jump = true;
    else if (space && std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - last_jump).count() > jump_cool_ms) trigger_jump = true;

    if (trigger_jump && vars::misc::jump_on_air) {
        uintptr_t humanoid = utils::find_first_child_byclass(character, "Humanoid");
        float jumpPower = vars::misc::jump_power_value;
        if (humanoid) { try { jumpPower = Read<float>(humanoid + offsets::JumpPower); } catch(...) {} }
        try { Vector vel = Read<Vector>(prim + offsets::Velocity); vel.y = jumpPower; Write<Vector>(prim + offsets::Velocity, vel); } catch(...) {}
        if (humanoid) { try { Write<bool>(humanoid + offsets::AutoJumpEnabled, true); } catch(...) {} const char* jumpNames[] = { "JumpRequest","Jump","AutoJump" }; for (const char* jn : jumpNames) { uintptr_t jd = utils::find_first_child(humanoid, jn); if (jd) try { Write<bool>(jd + offsets::Value, true); } catch(...) {} } }
        last_jump = std::chrono::steady_clock::now();
    }

    // Compute gravity to write: by default use gravity_factor * saved_gravity when space held
    if (space) {
        float factor = vars::misc::gravity_factor;
        float new_grav = saved_gravity * factor;
        // If jump_on_air is enabled and space is held, allow upward pull to combine behavior
        if (vars::misc::jump_on_air && vars::misc::jump_air_factor > 0.0f) {
            // blend between upward pull and gravity_factor behavior: if gravity_factor >=0 use both
            float up_pull = -std::abs(saved_gravity) * vars::misc::jump_air_factor;
            // choose stronger upward effect: if gravity_factor==0, use up_pull; otherwise blend
            if (factor <= 0.0f) new_grav = up_pull;
            else {
                // blend: weighted average favoring up_pull for immediate ascent
                new_grav = (new_grav * (1.0f - vars::misc::jump_air_factor)) + (up_pull * vars::misc::jump_air_factor);
            }
        }
        try { Write<float>(prim + offsets::PrimitiveGravity, new_grav); }
        catch (...) {}
    }
    else {
        // restore original gravity immediately when space released
        try { Write<float>(prim + offsets::PrimitiveGravity, saved_gravity); }
        catch (...) {}
    }

    prev_space = space;
}

void CMisc::jump_in_air()
{
    // Deprecated: jump_in_air behaviour is integrated into gravity_control().
    // Keep empty stub for compatibility.
    (void)0;
}

//void CMisc::anti_afk()
//{
// if (vars::misc::anti_afk_enabled) {
// write<float>(offsets::ForceNewAFKDuration, 999.0f);
// }
//}

//void CMisc::press_left_click()
//{
//    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
//    Sleep(5);
//    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
//	notify("Parried", ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
//}
//
//void CMisc::autoparry()
//{
//    if (!globals::local_player || !vars::autoparry::enabled)
//        return;
//
//    uintptr_t character = utils::get_model_instance(globals::local_player);
//    if (!character)
//        return;
//
//    uintptr_t highlight = utils::find_first_child(character, "Highlight");
//    if (!highlight)
//        return;
//
//    uintptr_t workspace = utils::find_first_child_byclass(globals::datamodel, "Workspace");
//    if (!workspace)
//        return;
//
//    uintptr_t root_part = utils::find_first_child(character, "HumanoidRootPart");
//    if (!root_part)
//        return;
//
//    uintptr_t player_primitive = read<uintptr_t>(root_part + offsets::Primitive);
//    if (!player_primitive)
//        return;
//
//    Vector player_position = read<Vector>(player_primitive + offsets::Position);
//
//    auto workspace_children = utils::children(workspace);
//
//    for (auto& folder : workspace_children)
//    {
//        if (!folder)
//            continue;
//
//        std::string class_name = utils::get_instance_classname(folder);
//        if (class_name != "Folder")
//            continue;
//
//        auto balls = utils::children(folder);
//        for (auto& ball : balls)
//        {
//            if (!ball)
//                continue;
//
//            uintptr_t primitive = read<uintptr_t>(ball + offsets::Primitive);
//            if (!primitive)
//                continue;
//
//            Vector position = read<Vector>(primitive + offsets::Position);
//            float distance = (position - player_position).Length();
//
//            if (distance <= vars::autoparry::parry_distance)
//            {
//                press_left_click();
//                return;
//            }
//        }
//    }
//}

void CMisc::rapid_fire()
{
    static bool mouseDown = false;
    if (!vars::misc::rapid_fire) return;

    uintptr_t players = utils::find_first_child_byclass(globals::datamodel, "Players");
    std::string local_name = utils::get_instance_name(globals::local_player);
    uintptr_t character = utils::get_model_instance(globals::local_player);
    if (!character)
        return;

    uintptr_t bodyeffects = utils::find_first_child(character, "BodyEffects");
    if (!bodyeffects)
        return;
    //printf("BodyEffects: %p\n", bodyeffects);
    if (!bodyeffects) return;
    uintptr_t gunfiring = FindFirstChild(bodyeffects, "GunFiring");
    //printf("GunFiring: %p\n", gunfiring);
    if (!gunfiring) return;

    bool isFiring = Read<bool>(gunfiring + offsets::Value);

    if (isFiring && !mouseDown)
    {
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        SendInput(1, &input, sizeof(input));
        mouseDown = true;
    }
    else if (!isFiring && mouseDown)
    {
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(1, &input, sizeof(input));
        mouseDown = false;
    }
}

void CMisc::antistomp_realud()
{
    bool antistomptoggle = vars::misc::antistomp;
    if (!antistomptoggle)
        return;

    uintptr_t character = utils::get_model_instance(globals::local_player);
    if (!character)
        return;

    uintptr_t bodyeffects = utils::find_first_child(character, "BodyEffects");
    if (!bodyeffects)
        return;

    uintptr_t ko = utils::find_first_child(bodyeffects, "K.O");
    if (!ko)
        return;

    bool is_knocked = Read<bool>(ko + offsets::Value);
    if (is_knocked)
    {
        uintptr_t humanoid = utils::find_first_child_byclass(character, "Humanoid");
        uintptr_t parent = Read<uintptr_t>(character + offsets::Parent);
        Write<uintptr_t>(character + offsets::Parent, 0);
        Write<float>(humanoid + offsets::Health, 0);
    }
}

void CMisc::headless()
{
    if (!globals::local_player)
        return;

    if (!vars::misc::headless)
        return;

    uintptr_t character = utils::get_model_instance(globals::local_player);
    if (!character)
        return;

    uintptr_t head = utils::find_first_child(character, "Head");
    if (!head)
        return;

    Write<float>(head + offsets::Transparency, 1.0f);

    std::vector<uintptr_t> head_children = utils::children(head);
    for (auto& child : head_children) {
        std::string name = utils::get_instance_name(child);
        std::string class_name = utils::get_instance_classname(child);
        if (name == "face" && (class_name == "Decal" || class_name == "Texture")) {
            Write<float>(child + offsets::Transparency, 1.0f);
        }
    }
}

void CMisc::phase_through_walls()
{
    if (!globals::local_player) return;
    if (!vars::misc::phase_through_walls) return;

    uintptr_t character = utils::get_model_instance(globals::local_player);
    if (!character) return;

    // Find parts in character and disable CanCollide / set CollisionGroup or move slightly through collisions
    std::vector<std::string> partNames = { "HumanoidRootPart", "Torso", "UpperTorso", "LowerTorso", "LeftUpperLeg", "RightUpperLeg", "LeftUpperArm", "RightUpperArm" };
    for (const auto& pname : partNames)
    {
        uintptr_t part = utils::find_first_child(character, pname.c_str());
        if (!part) continue;
        uintptr_t prim = Read<uintptr_t>(part + offsets::Primitive);
        if (!prim) continue;
        // disable collision
        Write<bool>(prim + offsets::CanCollide, false);
        // optionally reduce size slightly to avoid getting stuck
        // Try nudging position to slip through geometry when space pressed
        if (GetAsyncKeyState(VK_SPACE) & 0x8000)
        {
            Vector pos = Read<Vector>(prim + offsets::Position);
            pos.y += 0.025f;
            Write<Vector>(prim + offsets::Position, pos);
        }
    }
}

void CMisc::prevent_knockback()
{
    if (!globals::local_player) return;
    if (!vars::misc::prevent_knockback_enabled) return;

    uintptr_t character = utils::get_model_instance(globals::local_player);
    if (!character) return;

    // Try to force Humanoid state to prevent being knocked down
    auto humanoid = utils::find_first_child_byclass(character, "Humanoid");
    if (!humanoid) return;
    // Try clearing Sit/state id if present
    try { Write<bool>(humanoid + offsets::Sit, false); }
    catch (...) {}
    try { Write<int>(humanoid + offsets::HumanoidStateId, 0); }
    catch (...) {}

    // Handle velocity more intelligently: avoid zeroing while player is moving
    uintptr_t root = utils::find_first_child(character, "HumanoidRootPart");
    if (!root) return;
    uintptr_t prim = Read<uintptr_t>(root + offsets::Primitive);
    if (!prim) return;

    // read current velocity
    Vector vel = Read<Vector>(prim + offsets::Velocity);

    // consider player is moving if WASD is held
    bool moving = (GetAsyncKeyState('W') & 0x8000) || (GetAsyncKeyState('A') & 0x8000) || (GetAsyncKeyState('S') & 0x8000) || (GetAsyncKeyState('D') & 0x8000);

    // compute magnitude
    float magnitude = sqrtf(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z);

    // If large impulse detected (knockback) and player NOT moving, remove horizontal force and cancel vertical fall
    const float KNOCKBACK_THRESHOLD = 8.0f; // tweakable
    if (magnitude > KNOCKBACK_THRESHOLD && !moving) {
        // remove horizontal impulse, keep small base movement
        vel.x = 0.0f;
        vel.z = 0.0f;
        // cancel downward velocity
        if (vel.y < 0.0f) vel.y = 0.0f;
        try { Write<Vector>(prim + offsets::Velocity, vel); }
        catch (...) {}

        // nudge position upward slightly to avoid ground collision
        Vector pos = Read<Vector>(prim + offsets::Position);
        pos.y += 0.09f;
        try { Write<Vector>(prim + offsets::Position, pos); }
        catch (...) {}
    }
    else {
        // If moving, only clamp extreme vertical velocity to avoid being launched
        if (magnitude > KNOCKBACK_THRESHOLD && moving) {
            if (vel.y < -4.0f) vel.y = -4.0f; // limit downward launch
            try { Write<Vector>(prim + offsets::Velocity, vel); }
            catch (...) {}
        }
    }
}

void CMisc::NoJumpCoolDown()
{
    if (!globals::local_player)
        return;

    uintptr_t character = utils::get_model_instance(globals::local_player);
    if (!character)
        return;

    uintptr_t humanoid = utils::find_first_child_byclass(character, "Humanoid");
    if (!humanoid)
        return;

    if (vars::misc::nojumpcooldown) {
        // enable AutoJump to reduce cooldowns instead of writing to JumpPower (which is a float)
        Write<bool>(humanoid + offsets::AutoJumpEnabled, true);
    }
    else {
        Write<bool>(humanoid + offsets::AutoJumpEnabled, false);
    }
}
