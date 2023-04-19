#pragma once
#include <cstdint>
#include <Windows.h>
#include <cstddef>
#include <vector>

namespace ac::memory {
	template <class T>
	inline T read( const std::uintptr_t addr ) {
		DWORD op;
		VirtualProtect( reinterpret_cast< void* >( addr ), sizeof( T ), PAGE_EXECUTE_READWRITE, &op );
		const auto val = *reinterpret_cast< T* >( addr );
		VirtualProtect( reinterpret_cast< void* >( addr ), sizeof( T ), op, &op );
		return val;
	}	

	template <class T>
	inline void write( const std::uintptr_t addr, const T& value ) {
		DWORD op;
		VirtualProtect( reinterpret_cast< void* >( addr ), sizeof( T ), PAGE_EXECUTE_READWRITE, &op );
		*reinterpret_cast< T* >( addr ) = value;
		VirtualProtect( reinterpret_cast< void* >( addr ), sizeof( T ), op, &op );
	}
	
	inline void write( const std::uintptr_t addr, const void* value, const std::size_t size ) {
		DWORD op;
		VirtualProtect( reinterpret_cast< void* >( addr ), size, PAGE_EXECUTE_READWRITE, &op );
		std::memcpy( reinterpret_cast< void* >( addr ), value, size );
		VirtualProtect( reinterpret_cast< void* >( addr ), size, op, &op );
	}
	
	inline void read( const std::uintptr_t addr, void* value, const std::size_t size ) {
		DWORD op;
		VirtualProtect( reinterpret_cast< void* >( addr ), size, PAGE_EXECUTE_READWRITE, &op );
		std::memcpy( value, reinterpret_cast< void* >( addr ), size );
		VirtualProtect( reinterpret_cast< void* >( addr ), size, op, &op );
	}
}
