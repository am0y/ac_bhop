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
#include <asmjit/asmjit.h>

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

constexpr float ground_friction = 10.50f;

constexpr float max_air_vel = 0.10f;
constexpr float max_ground_vel = 2.f; 

constexpr float air_acceleration = 150.f;
constexpr float ground_acceleration = 10.f;

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
		
	float curr_speed = glm::dot( prev_vel, accel_dir );
	float add_speed = glm::clamp( max_velocity - curr_speed, 0.f, accelerate * dt );

	const auto res = prev_vel + add_speed * accel_dir;
	return ac::vec3_t( res.x, res.y, res.z );
}

ac::vec3_t mov_ground( ac::vec3_t accelDir, ac::vec3_t prevVelocity, float dt )
{
	glm::vec3 prev_vel( prevVelocity.x, prevVelocity.y, prevVelocity.z );
	glm::vec3 accel_dir( accelDir.x, accelDir.y, accelDir.z );

	float speed = glm::length( glm::vec3{ prev_vel.x, prev_vel.y, 0.f } ); // xy only
	if ( speed > 0.f )
	{
		float drop = speed * ground_friction * dt;
		auto modifier = ( fmaxf( speed - drop, 0 ) / speed );
		prev_vel.x *= modifier;
		prev_vel.y *= modifier;
	}

	return mov_accelerate( { accel_dir.x, accel_dir.y, accel_dir.z }, { prev_vel.x, prev_vel.y, prev_vel.z }, ground_acceleration, max_ground_vel, dt );
}

ac::vec3_t mov_air( ac::vec3_t accelDir, ac::vec3_t prevVelocity, float dt )
{
	return mov_accelerate( accelDir, prevVelocity, air_acceleration, max_air_vel, dt );
}

static asmjit::JitRuntime rt;

class asmjit_exception: public std::exception {
private:
	const char* m;

public:
	asmjit_exception( asmjit::Error err ) {
		m = asmjit::DebugUtils::errorAsString( err );
	}

	const char* what( ) {
		return m;
	}
};

using assembler_fn = std::function<void( asmjit::CodeHolder&, asmjit::x86::Assembler& )>;
std::pair<std::uintptr_t, std::size_t> jit_assemble( assembler_fn f ) {
	asmjit::CodeHolder code;
	code.init( rt.environment( ) );

	asmjit::x86::Assembler a( &code );
	f( code, a );

	std::uintptr_t ptr_to_code;
	std::size_t code_sz = code.codeSize( );
	asmjit::Error err = rt.add( &ptr_to_code, &code );
	if ( err ) throw asmjit_exception{ err };

	return std::make_pair( ptr_to_code, code_sz );
}

void midhook( std::uintptr_t target, std::size_t preserve_sz, assembler_fn f = nullptr ) {
	constexpr std::size_t jmp_len = 6;
	if ( preserve_sz < jmp_len ) return; // TO-DO: throw an exception?

	// Preserve bytes
	std::vector<std::uint8_t> preserved_bytes{ };
	preserved_bytes.resize( preserve_sz );
	ac::memory::read( target, preserved_bytes.data( ), preserve_sz );

	// NOP bytes for our jmp later
	const auto [i_nop_ptr, i_nop_sz] = jit_assemble( [ &preserve_sz ] ( asmjit::CodeHolder& code, asmjit::x86::Assembler& assembler ) -> void {
		for ( auto i = 0u; i < preserve_sz; i++ ) assembler.nop( );
	} );
	ac::memory::write( target, reinterpret_cast< void* >( i_nop_ptr ), i_nop_sz );

	// assemble our midhook
	const auto [i_midhook_ptr, i_midhook_sz] = jit_assemble( [ &preserve_sz, &f, &target ] ( asmjit::CodeHolder& code, asmjit::x86::Assembler& assembler ) -> void {
		for ( auto i = 0u; i < preserve_sz; i++ ) assembler.nop( ); // allocating instruction space for our preserved bytes
		if ( f ) f( code, assembler ); // assemble whatever instructions
		auto jmp_loc = reinterpret_cast< std::uintptr_t >( new std::uintptr_t{ target + preserve_sz } ); // allocate a pointer that holds jmp location
		assembler.jmp( asmjit::x86::dword_ptr( jmp_loc ) ); // jmp back to original code
	} );

	// write our preserved bytes to midhook
	ac::memory::write( i_midhook_ptr, preserved_bytes.data( ), preserve_sz );

	// assemble our trampoline
	const auto [i_jmp_ptr, i_jmp_sz] = jit_assemble( [ &i_midhook_ptr ] ( asmjit::CodeHolder& code, asmjit::x86::Assembler& assembler ) -> void {
		auto jmp_loc = reinterpret_cast< std::uintptr_t >( new std::uintptr_t{ i_midhook_ptr } ); // allocate a pointer that holds jmp location
		assembler.jmp( asmjit::x86::dword_ptr( jmp_loc ) ); // jmp to our midhook
	} );

	// write our trampoline to original code
	ac::memory::write( target, reinterpret_cast< void* >( i_jmp_ptr ), i_jmp_sz );
}

static float ac_frame_dt = 0.f;
constexpr float ac_friction = 1000000.f; // ac friction when source physics are applied
static float ac_fpsfric = 0.f;
static ac::vec3_t original_plr_vel{ 0.0f, 0.0f, 0.0f };
__declspec( noinline ) void update_dt( int curtime, std::uintptr_t physent_ptr ) {
	auto physent = reinterpret_cast<ac::physent_t*>( physent_ptr + sizeof( void* ) );
	if ( !( physent == ac::funcs::get_local_physent( ) ) ) return; // only care for localplayer!

	ac_frame_dt = static_cast<float>( curtime ) / 1000.f;
	ac_fpsfric = std::fmaxf( ac_friction / curtime * 20.0f, 1.0f );	
	original_plr_vel = physent->vel;
}

__declspec( noinline ) void run_physics( std::uintptr_t physent_ptr ) {
	auto physent = reinterpret_cast< ac::physent_t* >( physent_ptr + sizeof( void* ) );
	if ( !( physent == ac::funcs::get_local_physent( ) ) ) return; // only care for localplayer!
	if ( !states.is_enabled( "Source movements" ) ) return; // don't run if source is disabled!

	physent->vel.x = original_plr_vel.x * (ac_fpsfric - 1.f);
	physent->vel.x /= ac_fpsfric;
	physent->vel.y = original_plr_vel.y * (ac_fpsfric - 1.f);
	physent->vel.y /= ac_fpsfric;
}

static void* on_inputcheck_o = nullptr;
void on_inputcheck( ) {
	const auto original = *reinterpret_cast< decltype( on_inputcheck )* >( on_inputcheck_o );
	original( );

	static auto states_begin_t = std::chrono::high_resolution_clock::now( );
	auto t = std::chrono::high_resolution_clock::now( );

	auto states_dt = std::chrono::duration_cast< std::chrono::milliseconds >( t - states_begin_t ).count( );
	if ( states.run( states_dt ) ) {
		states_begin_t = t;
	}

	const auto localplayer = ac::funcs::get_local_physent( );

	if ( states.is_enabled( "Auto-hopping" ) && ( local_dir.z > 0.0f ) && localplayer->onfloor ) {
		localplayer->onfloor = false;
		localplayer->vel.z = 2.f;
	}

	if ( !states.is_enabled( "Source movements" ) ) return;

	ac::vec3_t accel_dir;
	auto move = local_dir.x;
	auto strafe = local_dir.y;

	accel_dir.x = ( float )( move * cosf( RAD * ( localplayer->yaw - 90 ) ) );
	accel_dir.y = ( float )( move * sinf( RAD * ( localplayer->yaw - 90 ) ) );
	accel_dir.z = 0.0f;

	accel_dir.x += ( float )( strafe * cosf( RAD * ( localplayer->yaw - 180 ) ) );
	accel_dir.y += ( float )( strafe * sinf( RAD * ( localplayer->yaw - 180 ) ) );

	auto acceleration_func = ( localplayer->onfloor || localplayer->onladder ) ? mov_ground : mov_air;
	localplayer->vel = acceleration_func( accel_dir, localplayer->vel, ac_frame_dt );
}

void begin_hooks() {
	MH_Initialize( );

	// Hooks to send input to our source physics calculations
	MH_CreateHook( ac::rebase<void*>( ac::update::plrbackward ), reinterpret_cast< void* >( on_backward ), &on_backward_o );
	MH_CreateHook( ac::rebase<void*>( ac::update::plrforward ), reinterpret_cast< void* >( on_forward ), &on_forward_o );
	MH_CreateHook( ac::rebase<void*>( ac::update::plrjump ), reinterpret_cast< void* >( on_jump ), &on_jump_o );
	MH_CreateHook( ac::rebase<void*>( ac::update::plrleft ), reinterpret_cast< void* >( on_left ), &on_left_o );
	MH_CreateHook( ac::rebase<void*>( ac::update::plrright ), reinterpret_cast< void* >( on_right ), &on_right_o );

	// Hook to run our state_t
	MH_CreateHook( ac::rebase<void*>( ac::update::checkinput ), reinterpret_cast< void* >( on_inputcheck ), &on_inputcheck_o );
	MH_EnableHook( ac::rebase<void*>( ac::update::checkinput ) );

	// Midfunc hooks for movement
	midhook( ac::rebase<std::uintptr_t>( ac::update::moveplr_fric0 ), ac::update::moveplr_fric0_sz, 
		[ ] ( asmjit::CodeHolder& code, asmjit::x86::Assembler& assembler ) -> void {
			using namespace asmjit::x86;

			/*
			    ...
				.text:004C19CD                 mov     esi, ecx                ; ptr to playerent
				.text:004C19CF                 mov     [esp+0ECh+var_8C], edx
				.text:004C19D3                 xor     dh, dh
				.text:004C19D5                 push    edi
				.text:004C19D6                 mov     edi, [ebp+curtime]      ; curtime
			*/

			assembler.push( esi ); // push esi
			assembler.push( edi ); // push edi			
			assembler.call( update_dt ); // call update_dt
			assembler.add( esp, 0x4 * 2 ); // add esp, 0x8

			/*
				.text:004C19D9                 mov     cl, [esi+77h]
				.text:004C19DC                 mov     ah, [esi+76h]
				.text:004C19DF                 mov     [esp+0F0h+var_E3], dh
				.text:004C19E3                 mov     [esp+0F0h+var_D1], ah
				.text:004C19E7                 test    cl, cl
				...
			*/
		} 
	);
	midhook( ac::rebase<std::uintptr_t>( ac::update::moveplr_fric1 ), ac::update::moveplr_fric1_sz, 
		[ ] ( asmjit::CodeHolder& code, asmjit::x86::Assembler& assembler ) -> void {
			using namespace asmjit::x86;

			/*
			    ...
				.text:004C21C9                 addss   xmm0, xmm4
				.text:004C21CD                 mulss   xmm2, xmm1
				.text:004C21D1                 mulss   xmm0, xmm1
				.text:004C21D5                 movss   dword ptr [esi+18h], xmm2
				.text:004C21DA                 movss   dword ptr [esi+14h], xmm0
			*/

			assembler.push( esi ); // push esi	
			assembler.call( run_physics ); // call update_dt
			assembler.add( esp, 0x4 * 1 ); // add esp, 0x4

			/*
				.text:004C21DF                 movq    xmm0, qword ptr [esi+10h]
				.text:004C21E4                 mov     eax, [esi+18h]
				.text:004C21E7                 movq    [esp+0F0h+var_B4], xmm0
				.text:004C21ED                 movss   xmm2, dword ptr [esp+0F0h+var_B4]
				.text:004C21F3                 movss   xmm1, dword ptr [esp+0F0h+var_B4+4]
				...
			*/
		} 
	);
}

void ac::client::main( )
{
	// Init console
	AllocConsole( );
	freopen_s( reinterpret_cast< FILE** >( stdout ), "CONOUT$", "w", stdout );
	freopen_s( reinterpret_cast< FILE** >( stderr ), "CONOUT$", "w", stderr );
	freopen_s( reinterpret_cast< FILE** >( stdin ), "CONIN$", "r", stdin );	
	SetConsoleTitle( "AssaultCube - Bhop" );	

	begin_hooks( );
	
	std::cout << "Hold SPACE - Autohop\n";	
	std::cout << "\n";
		
	std::cout << "Press F5 - Toggle autohop\n";		
	std::cout << "Press F6 - Toggle Source movements\n";		
	std::cout << "\n";		
}
