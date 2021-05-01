/**
 * Open Space Program
 * Copyright � 2019-2020 Open Space Program Project
 *
 * MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#pragma once

#include "../Universe.h"
#include "../aligned_allocator.h"

#include <type_traits>
#include <memory>
#include <tuple>
#include <Corrade/Containers/ArrayView.h>
#include <Corrade/Containers/StridedArrayView.h>

namespace osp::universe
{

struct UCompEmitsGravity {};
struct UCompSignificantBody {};
struct UCompInsignificantBody {};

constexpr double G = 6.674e-11;

/*
Table structure (concept):

| Body 1 | Body 2 |  ...  | Body N |
| Vxyz,M | Vxyz,M |  ...  | Vxyz,M | Vel, mass, metadata
|--------|--------|-------|--------|
| X,Y,Z  | X,Y,Z  |  ...  | X,Y,Z  | Step 1
| X,Y,Z  | X,Y,Z  |  ...  | X,Y,Z  | Step 2
|        |        |       |        | ...
| X,Y,Z  | X,Y,Z  |  ...  | X,Y,Z  | Step N

Table structure (implementation):

| Body 1 | Body 2 |  ...  | Body N | Metadata
| M      | M      |  ...  | M      | Masses
| Vxyz   | Vxyz   |  ...  | Vxyz   | Velocities

| X[N] || Y[N] || Z[N] || Step 1   (|| == padding)
| X[N] || Y[N] || Z[N] || Step 2
| X[N] || Y[N] || Z[N] || Step 3

*/
class EvolutionTable
{
    friend class TrajNBody;
public:
    EvolutionTable(size_t nBodies, size_t nSteps);
    EvolutionTable();
    ~EvolutionTable() = default;
    EvolutionTable(EvolutionTable const&) = delete;
    EvolutionTable(EvolutionTable&& move) = default;

    void resize(size_t bodies, size_t timesteps);

    Satellite get_ID(size_t index) const;
    void set_ID(size_t index, Satellite sat);

    double get_mass(size_t index) const;
    void set_mass(size_t index, double mass);

    Vector3d get_velocity(size_t index) const;
    void set_velocity(size_t index, Vector3d vel);

    Vector3d get_acceleration(size_t index) const;
    void set_acceleration(size_t index, Vector3d accel);

    Vector3d get_position(size_t index, size_t timestep) const;
    void set_position(size_t index, size_t timestep, Vector3d pos);

    struct SystemState
    {
        struct vec3ptrs { double* x; double* y; double* z; };
        // Positions
        vec3ptrs m_position;
        vec3ptrs m_velocity;
        vec3ptrs m_acceleration;
        double* m_masses;
        size_t m_nElements;
        size_t m_paddedArraySize;
    };

    SystemState get_system_state(size_t timestep);

    struct RawStepData
    {
        Corrade::Containers::ArrayView<double> m_data;
        size_t m_nElements;
        size_t m_nElementsPadded;
    };

    RawStepData get_step(size_t timestep);

    struct TableColumn
    {
        Corrade::Containers::StridedArrayView1D<double> m_x;
        Corrade::Containers::StridedArrayView1D<double> m_y;
        Corrade::Containers::StridedArrayView1D<double> m_z;
        size_t m_currentStep;
    };

    TableColumn get_column(size_t index);

    bool is_in_table(Satellite sat) const;

    size_t get_index(Satellite sat) const;

    //void copy_step_to_top(size_t timestep);
private:
    static constexpr size_t AVX2_WIDTH = 256 / 8;  // 256 bits / 8 = 32 Bytes

    template <typename T>
    static size_t padded_size_aligned(size_t nElements)
    {
        constexpr size_t avxLanes = AVX2_WIDTH / sizeof(T);
        size_t padding = avxLanes - (nElements % avxLanes);
        return (nElements + padding) * sizeof(T);
    }

    // Allocate an aligned array of
    template <typename T>
    static T* alloc_raw(size_t size)
    {
        //static_assert(std::is_arithmetic<T>::value, "Can only allocate arithmetic types");

        return AlignedAllocator<T, AVX2_WIDTH>::allocate(size);
    }

    template <typename T>
    static T* alloc_aligned(size_t nElements)
    {
        //static_assert(std::is_arithmetic<T>::value, "Can only allocate arithmetic types");
        size_t size = padded_size_aligned<T>(nElements);
        return AlignedAllocator<T, AVX2_WIDTH>::allocate(size);
    }

    // Deleter functions
    template<typename T>
    static void del_aligned(T* ptr)
    {
        AlignedAllocator<T, AVX2_WIDTH>::deallocate(ptr, sizeof(ptr));
    }

    // std::unique_ptr alias with custom aligned alloc deleter
    template <typename T>
    class table_ptr : public std::unique_ptr<T[], decltype(&del_aligned<T>)>
    {
    private:
        using ptr_type = std::unique_ptr<T[], decltype(&del_aligned<T>)>;
    public:
        table_ptr(size_t elements)
            : ptr_type(
                alloc_aligned<T>(elements),
                &del_aligned<T>)
        {}

        table_ptr(T* ptr) : ptr_type(ptr, &del_aligned<T>) {}
    };

    // Table dimensions

    size_t m_numBodies{0};
    size_t m_numTimesteps{0};
    size_t m_currentStep{0};

    size_t m_scalarArraySizeBytes;
    size_t m_rowSizeBytes;

    // Current step only/static tables

    table_ptr<Satellite> m_ids;
    table_ptr<double> m_masses;

    table_ptr<double> m_velocities;
    table_ptr<double> m_accelerations;

    // Table of positions over time
    table_ptr<double> m_posTable;
};

/**
 * A not very static universe where everything moves constantly
 */
class TrajNBody : public CommonTrajectory<TrajNBody>
{
public:
    static constexpr double smc_timestep = 1'000.0;

    TrajNBody(Universe& rUni, Satellite center);
    ~TrajNBody() = default;

    // (not yet implemented) Update universe registry data from evolution table
    //static void update_world();

    // Simulate orbits
    void update(/*Universe& rUni*/); // TODO make static

    void build_table();

    using FullState_t = std::pair<EvolutionTable::RawStepData, EvolutionTable::RawStepData>;
    FullState_t get_latest_state();

    bool is_in_table(Satellite sat);

    EvolutionTable::TableColumn get_column(Satellite sat);

    Vector3d get_futuremost_location(Satellite sat) const;

    size_t num_future_steps() const;

    constexpr bool updated_last_frame() const noexcept { return m_updatedLastFrame; }
public:
    template <typename VIEW_T, typename SRC_VIEW_T>
    static void update_full_dynamics_acceleration(VIEW_T& bodyView, SRC_VIEW_T& sources);

    template <typename VIEW_T>
    static void update_full_dynamics_kinematics(VIEW_T& view);

    void solve_table();

    void solve_nbody_timestep(size_t stepIndex);
    void solve_nbody_timestep_AVX(size_t stepIndex);

    void solve_insignificant_bodies(size_t inputStepIndex);
    void solve_insignificant_bodies_AVX(size_t inputStepIndex);

    void write_universe_components(EvolutionTable& dataSource);

    EvolutionTable m_nBodyData;
    EvolutionTable m_insignificantBodyData;
    double m_timestepElapsed{0.0};
    bool m_updatedLastFrame{true};
};

}