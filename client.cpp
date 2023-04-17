#include "client.hpp"

#include <Windows.h>
#include <iostream>
#include <thread>
#include <vector>
#include <functional>
#include <mutex>
#include <cmath>

#include "sdk.hpp"
#include "memory.hpp"

#include <MinHook.h>
#include <glm/vec3.hpp>
#include <glm/geometric.hpp>

#define RAD   (0.01745329252f)

struct state_t {	
	std::string name;
	std::uint8_t key;
	
	bool enabled = false;
	std::function<void( bool )> callback = nullptr;
};

class states_t {
private:
	std::vector< state_t > states{ };

public:
	states_t( const std::vector< state_t >& states ): states( states ) { }

	bool run( long long dt ) {
		if ( dt < 250 ) return false;
		
		bool changed = false;
		std::for_each( states.begin( ), states.end( ), [ &changed ] ( state_t& state ) {
			if ( GetAsyncKeyState( state.key ) & 0x8000 ) {
				state.enabled = !state.enabled;
				std::cout << state.name << ": " << ( state.enabled ? "ON" : "OFF" ) << std::endl;
				if ( state.callback ) state.callback( state.enabled );
				changed = true;
			}
		} );
		
		return changed;
	}

	bool is_enabled( const std::string& name ) {
		const auto it = std::find_if( states.begin( ), states.end( ), [ & ] ( const state_t& state ) {
			return state.name == name;
		} );

		if ( it == states.end( ) ) return false;
		return it->enabled;
	}
};

static float air_acceleration = 0.0075f;
static float ground_acceleration = 0.01f;

static float ground_friction = 0.f;

static float max_ground_vel = 1.f;
static float max_air_vel = 10.f;

static states_t states( {
	{ "Auto-hopping", VK_F5, false },
	{ "Source movements", VK_F6, false, [ ] ( bool enabled ) { 
		auto f = enabled ? MH_EnableHook : MH_DisableHook;

		f( ac::rebase<void*>( ac::update::plrbackward ) );
		f( ac::rebase<void*>( ac::update::plrforward ) );
		f( ac::rebase<void*>( ac::update::plrjump ) );
		f( ac::rebase<void*>( ac::update::plrleft ) );
		f( ac::rebase<void*>( ac::update::plrright ) );
	} },
} );

static ac::vec3_t local_dir{ 0.f,0.f,0.f };

static void* on_backward_o = nullptr;
int on_backward( bool began ) {
	const auto original = *reinterpret_cast< decltype( on_backward )* >( on_backward_o );

	local_dir.x += ( began ? -1 : 1 );

	return original( false );
}
static void* on_forward_o = nullptr;
int on_forward( bool began ) {
	const auto original = *reinterpret_cast< decltype( on_forward )* >( on_forward_o );

	local_dir.x += ( began ? 1 : -1 );

	return original( false );
}
static void* on_right_o = nullptr;
int on_right( bool began ) {
	const auto original = *reinterpret_cast< decltype( on_right )* >( on_right_o );

	local_dir.y += ( began ? -1 : 1 );

	return original( false );
}
static void* on_left_o = nullptr;
int on_left( bool began ) {
	const auto original = *reinterpret_cast< decltype( on_left )* >( on_left_o );

	local_dir.y += ( began ? 1 : -1 );

	return original( false );
}
static void* on_jump_o = nullptr;
int on_jump( bool began ) {
	const auto original = *reinterpret_cast< decltype( on_jump )* >( on_jump_o );
	local_dir.z = ( began ? 1.f : 0.f );
	return original( began );
}

ac::vec3_t mov_accelerate( ac::vec3_t accelDir, ac::vec3_t prevVelocity, float accelerate, float max_velocity, float dt )
{
	glm::vec3 prev_vel( prevVelocity.x, prevVelocity.y, prevVelocity.z );
	glm::vec3 accel_dir( accelDir.x, accelDir.y, accelDir.z );	
		
	float projVel = glm::dot( prev_vel, accel_dir );
	float accelVel = accelerate * dt; 

	if ( (projVel + accelVel) > max_velocity )
		accelVel = max_velocity - projVel;

	//std::cout << "prev_vel: " << std::defaultfloat << prev_vel.x << ", " << prev_vel.y << ", " << prev_vel.z << "\n";
	//std::cout << "accel_dir: " << std::defaultfloat << accel_dir.x << ", " << accel_dir.y << ", " << accel_dir.z << "\n";
	//std::cout << std::defaultfloat << "projVel: " << projVel << "\n";
	//std::cout << std::defaultfloat << "accelVel: " << accelVel << "\n";
	//std::cout << std::defaultfloat << "dt: " << dt << "\n";
	//std::cout << "----------------------------\n";

	const auto res = prev_vel + accel_dir * accelVel;
	return ac::vec3_t( res.x, res.y, res.z );
}

ac::vec3_t mov_ground( ac::vec3_t accelDir, ac::vec3_t prevVelocity, float dt )
{
	glm::vec3 prev_vel( prevVelocity.x, prevVelocity.y, prevVelocity.z );
	glm::vec3 accel_dir( accelDir.x, accelDir.y, accelDir.z );

	float speed = prev_vel.length();
	if ( speed != 0 )
	{
		float drop = speed * ground_friction * dt;
		accel_dir *= fmaxf( speed - drop, 0 ) / speed;
	}

	return mov_accelerate( accelDir, prevVelocity, ground_acceleration, max_ground_vel, dt );
}

ac::vec3_t mov_air( ac::vec3_t accelDir, ac::vec3_t prevVelocity, float dt )
{
	return mov_accelerate( accelDir, prevVelocity, air_acceleration, max_air_vel, dt );
}
 
static void* on_inputcheck_o = nullptr;
void on_inputcheck( ) {	
	const auto original = *reinterpret_cast< decltype( on_inputcheck )* >( on_inputcheck_o );
	original( );

	static auto states_begin_t = std::chrono::high_resolution_clock::now( );
	static auto begin_t = std::chrono::high_resolution_clock::now( );
	auto t = std::chrono::high_resolution_clock::now( );
		
	auto states_dt = std::chrono::duration_cast< std::chrono::milliseconds >( t - states_begin_t ).count( );	
	if ( states.run( states_dt ) ) {
		states_begin_t = t;
	}

	auto dt = std::chrono::duration_cast< std::chrono::milliseconds >( t - begin_t ).count( );

	const auto localplayer = ac::funcs::get_local_physent( );

	if ( states.is_enabled( "Auto-hopping" ) && (local_dir.z > 0.0f) && localplayer->onfloor ) {
		localplayer->onfloor = false;
		localplayer->vel.z = 2.f;
	}

	if ( !states.is_enabled( "Source movements" ) ) return;

	// Now we do movements!
	auto frameDt = static_cast< float >( dt );

	ac::vec3_t accel_dir;
	auto prev_vel = localplayer->vel;
	auto move = local_dir.x;
	auto strafe = local_dir.y;

	accel_dir.x = ( float )( move * cosf( RAD * ( localplayer->yaw - 90 ) ) );
	accel_dir.y = ( float )( move * sinf( RAD * ( localplayer->yaw - 90 ) ) );
	accel_dir.z = 0.0f;

	accel_dir.x += ( float )( strafe * cosf( RAD * ( localplayer->yaw - 180 ) ) );
	accel_dir.y += ( float )( strafe * sinf( RAD * ( localplayer->yaw - 180 ) ) );

	accel_dir.x = std::fmaxf( std::fminf( accel_dir.x, 1.f ), -1.f );
	accel_dir.y = std::fmaxf( std::fminf( accel_dir.y, 1.f ), -1.f );

	auto acceleration_func = localplayer->onfloor ? mov_ground : mov_air;
	localplayer->vel = acceleration_func( accel_dir, prev_vel, frameDt );

	// We're done!!
	begin_t = t;
}

void ac::client::main( )
{
	// Init console
	AllocConsole( );
	freopen_s( reinterpret_cast< FILE** >( stdout ), "CONOUT$", "w", stdout );
	freopen_s( reinterpret_cast< FILE** >( stderr ), "CONOUT$", "w", stderr );
	freopen_s( reinterpret_cast< FILE** >( stdin ), "CONIN$", "r", stdin );	
	SetConsoleTitle( "AssaultCube - Bhop" );	

	MH_Initialize( );
	MH_CreateHook( ac::rebase<void*>( update::plrbackward ), reinterpret_cast<void*>(on_backward), &on_backward_o );
	MH_CreateHook( ac::rebase<void*>( update::plrforward ), reinterpret_cast< void* >( on_forward ), &on_forward_o );
	MH_CreateHook( ac::rebase<void*>( update::plrjump ), reinterpret_cast< void* >( on_jump ), &on_jump_o );
	MH_CreateHook( ac::rebase<void*>( update::plrleft ), reinterpret_cast< void* >( on_left ), &on_left_o );
	MH_CreateHook( ac::rebase<void*>( update::plrright ), reinterpret_cast< void* >( on_right ), &on_right_o );

	MH_CreateHook( ac::rebase<void*>( update::checkinput ), reinterpret_cast< void* >( on_inputcheck ), &on_inputcheck_o );
	MH_EnableHook( ac::rebase<void*>( update::checkinput ) );
	
	std::cout << "Hold SPACE - Autohop\n";	
	std::cout << "\n";
		
	std::cout << "Press F5 - Toggle autohop\n";		
	std::cout << "Press F6 - Toggle Source movements\n";		
	std::cout << "\n";		
}
