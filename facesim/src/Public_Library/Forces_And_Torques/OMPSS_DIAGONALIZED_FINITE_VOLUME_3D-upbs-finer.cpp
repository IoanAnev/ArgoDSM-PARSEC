//#####################################################################
// Copyright 2003-2004, Ron Fedkiw, Geoffrey Irving, Igor Neverov, Eftychios Sifakis, Joseph Teran.
// This file is part of PhysBAM whose distribution is governed by the license contained in the accompanying file PHYSBAM_COPYRIGHT.txt.
// OmpSs/OpenMP 4.0 versions by Raul Vidal Ortiz - Barcelona Supercomputing Center
//#####################################################################
#include <omp.h>
#include "OMPSS_DIAGONALIZED_FINITE_VOLUME_3D-upbs-finer.h"
#include "../Thread_Utilities/OMPSS_POOL.h"
#include "../Utilities/LOG.h"
#include "../Arrays/ARRAY_RANGE.h"
#include "../Arrays/ARRAYS_RANGE.h"
//#define USE_REDUCTION_ROUTINES
#define CACHELINE 64
#ifdef ENABLE_OMPEXTRAE
	#include "../Utilities/EXTRAE.h"
#endif
using namespace PhysBAM;
//#define OUTPUT_THREADING_AUXILIARY_STRUCTURES
//#define OUTPUT_BENCHMARK_DATA
//#####################################################################
// Function Update_Threading_Auxiliary_Structures
//#####################################################################
inline int Extended_Segment (const LIST_ARRAYS<int>& extended_edges, const LIST_ARRAY<LIST_ARRAY<int> >& incident_extended_edges, const int i, const int j)
{
	for (int k = 1; k <= incident_extended_edges (i).m; k++)
	{
		int e = incident_extended_edges (i) (k), m, n;
		extended_edges.Get (e, m, n);
		assert (m == i || n == i);

		if (m == j || n == j) return e;
	}

	return 0;
}
template<class T> void DIAGONALIZED_FINITE_VOLUME_3D<T>::
Update_Threading_Auxiliary_Structures()
{
#ifndef USE_REDUCTION_ROUTINES
	assert (strain_measure.tetrahedron_mesh.segment_mesh);

	if (!strain_measure.tetrahedron_mesh.segment_mesh->incident_segments) strain_measure.tetrahedron_mesh.segment_mesh->Initialize_Incident_Segments();

	if (!strain_measure.tetrahedron_mesh.neighbor_nodes) strain_measure.tetrahedron_mesh.Initialize_Neighbor_Nodes();

	if (!strain_measure.tetrahedron_mesh.incident_tetrahedrons) strain_measure.tetrahedron_mesh.Initialize_Incident_Tetrahedrons();

	LIST_ARRAY<LIST_ARRAY<int> >& neighbor_nodes = *strain_measure.tetrahedron_mesh.neighbor_nodes;
	LIST_ARRAY<LIST_ARRAY<int> >& incident_tetrahedrons = *strain_measure.tetrahedron_mesh.incident_tetrahedrons;
	assert (strain_measure.tetrahedron_mesh.tetrahedron_edges);
	LIST_ARRAYS<int>& tetrahedron_edges = *strain_measure.tetrahedron_mesh.tetrahedron_edges;
	LIST_ARRAYS<int>& tetrahedrons = strain_measure.tetrahedron_mesh.tetrahedrons;
	LIST_ARRAYS<int>& segments = strain_measure.tetrahedron_mesh.segment_mesh->segments;

	//OMPSS
	OMPSS_POOL& pool = *OMPSS_POOL::Singleton();


	if (threading_auxiliary_structures) delete threading_auxiliary_structures;

	threading_auxiliary_structures = new DIAGONALIZED_FINITE_VOLUME_3D_THREADING_AUXILIARY_STRUCTURES<T>;
	int number_of_nodes = strain_measure.tetrahedralized_volume.particles.number;
	threading_auxiliary_structures->node_ranges = new ARRAY<VECTOR_2D<int> >;
	ARRAY<VECTOR_2D<int> >& node_ranges = *threading_auxiliary_structures->node_ranges;
	node_ranges = *strain_measure.tetrahedralized_volume.particles.particle_ranges;
	LIST_ARRAY<LIST_ARRAY<int> > internal_higher_neighbors (number_of_nodes), external_neighbors (number_of_nodes);
	threading_auxiliary_structures->extended_edges = new LIST_ARRAYS<int> (2, 0);
	LIST_ARRAYS<int>& extended_edges = *threading_auxiliary_structures->extended_edges;
	//OMPSS
	threading_auxiliary_structures->internal_edge_ranges = new ARRAY<VECTOR_2D<int> > (pool.Get_n_divisions());
	ARRAY<VECTOR_2D<int> >& internal_edge_ranges = *threading_auxiliary_structures->internal_edge_ranges;
	threading_auxiliary_structures->external_edge_ranges = new ARRAY<VECTOR_2D<int> > (pool.Get_n_divisions());

	ARRAY<VECTOR_2D<int> >& external_edge_ranges = *threading_auxiliary_structures->external_edge_ranges;

	for (int p = 1; p <= node_ranges.m; p++)
	{
		for (int i = node_ranges (p).x; i <= node_ranges (p).y; i++)
		{
			for (int j = 1; j <= neighbor_nodes (i).m; j++)
				if (neighbor_nodes (i) (j) < node_ranges (p).x || neighbor_nodes (i) (j) > node_ranges (p).y)
					external_neighbors (i).Append_Element (neighbor_nodes (i) (j));
				else if (neighbor_nodes (i) (j) > i)
					internal_higher_neighbors (i).Append_Element (neighbor_nodes (i) (j));

			LIST_ARRAY<int>::sort (internal_higher_neighbors (i));
			LIST_ARRAY<int>::sort (external_neighbors (i));
		}

		internal_edge_ranges (p).x = extended_edges.m + 1;

		for (int i = node_ranges (p).x; i <= node_ranges (p).y; i++) for (int j = 1; j <= internal_higher_neighbors (i).m; j++) extended_edges.Append_Element (i, internal_higher_neighbors (i) (j));

		internal_edge_ranges (p).y = extended_edges.m;
		external_edge_ranges (p).x = extended_edges.m + 1;

		for (int i = node_ranges (p).x; i <= node_ranges (p).y; i++) for (int j = 1; j <= external_neighbors (i).m; j++) extended_edges.Append_Element (i, external_neighbors (i) (j));

		external_edge_ranges (p).y = extended_edges.m;
	}

	LIST_ARRAY<LIST_ARRAY<int> > incident_extended_edges (number_of_nodes);

	for (int p = 1; p <= node_ranges.m; p++)
	{
		for (int e = internal_edge_ranges (p).x; e <= internal_edge_ranges (p).y; e++)
		{
			int m, n;
			extended_edges.Get (e, m, n);
			incident_extended_edges (m).Append_Element (e);
			incident_extended_edges (n).Append_Element (e);
		}

		for (int e = external_edge_ranges (p).x; e <= external_edge_ranges (p).y; e++)
		{
			int m, n;
			extended_edges.Get (e, m, n);
			incident_extended_edges (m).Append_Element (e);
		}
	}

	threading_auxiliary_structures->extended_tetrahedrons = new LIST_ARRAYS<int> (4, 0);
	LIST_ARRAYS<int>& extended_tetrahedrons = *threading_auxiliary_structures->extended_tetrahedrons;
	//OMPSS
	threading_auxiliary_structures->extended_tetrahedron_ranges = new ARRAY<VECTOR_2D<int> > (pool.Get_n_divisions());

	ARRAY<VECTOR_2D<int> >& extended_tetrahedron_ranges = *threading_auxiliary_structures->extended_tetrahedron_ranges;
	threading_auxiliary_structures->extended_tetrahedron_extended_edges = new LIST_ARRAYS<int> (6, 0);
	LIST_ARRAYS<int>& extended_tetrahedron_extended_edges = *threading_auxiliary_structures->extended_tetrahedron_extended_edges;
	threading_auxiliary_structures->extended_tetrahedron_parents = new LIST_ARRAY<int>;
	LIST_ARRAY<int>& extended_tetrahedron_parents = *threading_auxiliary_structures->extended_tetrahedron_parents;

	for (int p = 1; p <= node_ranges.m; p++)
	{
		extended_tetrahedron_ranges (p).x = extended_tetrahedrons.m + 1;

		for (int i = node_ranges (p).x; i <= node_ranges (p).y; i++) for (int j = 1; j <= incident_tetrahedrons (i).m; j++)
			{
				ARRAY<int> vertex (4);
				tetrahedrons.Get (incident_tetrahedrons (i) (j), vertex (1), vertex (2), vertex (3), vertex (4));

				if ( (vertex (1) >= node_ranges (p).x && vertex (1) < i) || (vertex (2) >= node_ranges (p).x && vertex (2) < i) ||
						(vertex (3) >= node_ranges (p).x && vertex (3) < i) || (vertex (4) >= node_ranges (p).x && vertex (4) < i)) continue;

				extended_tetrahedrons.Append_Element (vertex (1), vertex (2), vertex (3), vertex (4));
				extended_tetrahedron_parents.Append_Element (incident_tetrahedrons (i) (j));
				ARRAY<int> edge (6);

				for (int v1 = 1, e = 1; v1 <= 3; v1++) for (int v2 = v1 + 1; v2 <= 4; v2++, e++)
					{
						if (vertex (v1) >= node_ranges (p).x && vertex (v1) <= node_ranges (p).y) edge (e) = Extended_Segment (extended_edges, incident_extended_edges, vertex (v1), vertex (v2));
						else if (vertex (v2) >= node_ranges (p).x && vertex (v2) <= node_ranges (p).y) edge (e) = Extended_Segment (extended_edges, incident_extended_edges, vertex (v2), vertex (v1));
						else edge (e) = 0;
					}

				extended_tetrahedron_extended_edges.Append_Element (edge (1), edge (2), edge (3), edge (4), edge (5), edge (6));
			}

		extended_tetrahedron_ranges (p).y = extended_tetrahedrons.m;
	}

	U.Resize_Array (0);
	De_inverse_hat.Resize_Array (0);
	Fe_hat.Resize_Array (0);

	if (edge_stiffness) delete edge_stiffness;

	edge_stiffness = 0;

	if (dP_dFe) delete dP_dFe;

	dP_dFe = 0;

	if (V) delete V;

	V = 0;
	threading_auxiliary_structures->extended_edge_stiffness = new LIST_ARRAY<MATRIX_3X3<T> >;
	threading_auxiliary_structures->extended_U = new LIST_ARRAY<MATRIX_3X3<T> >;
	threading_auxiliary_structures->extended_De_inverse_hat = new LIST_ARRAY<MATRIX_3X3<T> >;
	threading_auxiliary_structures->extended_Fe_hat = new LIST_ARRAY<DIAGONAL_MATRIX_3X3<T> >;
	threading_auxiliary_structures->extended_dP_dFe = new LIST_ARRAY<DIAGONALIZED_ISOTROPIC_STRESS_DERIVATIVE_3D<T> >;
	threading_auxiliary_structures->extended_V = new LIST_ARRAY<MATRIX_3X3<T> >;


#if defined OUTPUT_THREADING_AUXILIARY_STRUCTURES || defined OUTPUT_BENCHMARK_DATA
unsigned long N_divs = 0;
    //OMPSS
	N_divs = pool.Get_n_divisions();
    

    #ifdef OUTPUT_THREADING_AUXILIARY_STRUCTURES
        FILE_UTILITIES::Write_To_File<T> (STRING_UTILITIES::string_sprintf ("threading_auxiliary_structures_%d.dat", N_divs), *threading_auxiliary_structures);
        exit (0);
    #endif


    #ifdef OUTPUT_BENCHMARK_DATA
        FILE_UTILITIES::Write_To_File<T> (STRING_UTILITIES::string_sprintf ("extended_edges_%d.dat", N_divs), extended_edges);
        FILE_UTILITIES::Write_To_File<T> (STRING_UTILITIES::string_sprintf ("node_ranges_%d.dat", N_divs), node_ranges);
        FILE_UTILITIES::Write_To_File<T> (STRING_UTILITIES::string_sprintf ("internal_edge_ranges_%d.dat", N_divs), internal_edge_ranges);
        FILE_UTILITIES::Write_To_File<T> (STRING_UTILITIES::string_sprintf ("external_edge_ranges_%d.dat", N_divs), external_edge_ranges);
    #endif
#endif


#else
	edge_stiffness = new LIST_ARRAY<MATRIX_3X3<T> >;
#endif
}
template<class T> void DIAGONALIZED_FINITE_VOLUME_3D<T>::
Read_Threading_Auxiliary_Structures()
{
	NOT_IMPLEMENTED();
}

//#####################################################################
// Function Update_Position_Based_State
//#####################################################################
template<class T> void DIAGONALIZED_FINITE_VOLUME_3D<T>::
Update_Position_Based_State_Helper(unsigned long partition_id)
{
#ifndef USE_REDUCTION_ROUTINES
	DIAGONALIZED_FINITE_VOLUME_3D_THREADING_AUXILIARY_STRUCTURES<T>const& threading_auxiliary_structures = *this->threading_auxiliary_structures;
	LIST_ARRAYS<int>const& extended_edges = *threading_auxiliary_structures.extended_edges;
	LIST_ARRAYS<int>const& extended_tetrahedrons = *threading_auxiliary_structures.extended_tetrahedrons;
	LIST_ARRAY<MATRIX_3X3<T> >& extended_edge_stiffness = *threading_auxiliary_structures.extended_edge_stiffness;
	VECTOR_2D<int>const& node_range = (*threading_auxiliary_structures.node_ranges)(partition_id);
	VECTOR_2D<int>const& internal_edge_range = (*threading_auxiliary_structures.internal_edge_ranges)(partition_id);
	VECTOR_2D<int>const& external_edge_range = (*threading_auxiliary_structures.external_edge_ranges)(partition_id);
	VECTOR_2D<int>const& extended_tetrahedron_range = (*threading_auxiliary_structures.extended_tetrahedron_ranges)(partition_id);
	LIST_ARRAYS<int>const& extended_tetrahedron_extended_edges = *threading_auxiliary_structures.extended_tetrahedron_extended_edges;
	LIST_ARRAY<int>const& extended_tetrahedron_parents = *threading_auxiliary_structures.extended_tetrahedron_parents;
	LIST_ARRAY<MATRIX_3X3<T> >& extended_U = *threading_auxiliary_structures.extended_U;
	LIST_ARRAY<MATRIX_3X3<T> >& extended_De_inverse_hat = *threading_auxiliary_structures.extended_De_inverse_hat;
	LIST_ARRAY<DIAGONAL_MATRIX_3X3<T> >& extended_Fe_hat = *threading_auxiliary_structures.extended_Fe_hat;
	LIST_ARRAY<DIAGONALIZED_ISOTROPIC_STRESS_DERIVATIVE_3D<T> >& extended_dP_dFe = *threading_auxiliary_structures.extended_dP_dFe;
	LIST_ARRAY<MATRIX_3X3<T> >& extended_V = *threading_auxiliary_structures.extended_V;

    //int numnodes = node_range.y - node_range.x;
    //int numedges = external_edge_range.y - internal_edge_range.x;
    //int stride = CACHELINE/sizeof(omp_lock_t);
    //omp_lock_t* nodelocks = new omp_lock_t[numnodes*stride];
    //omp_lock_t* edgelocks = new omp_lock_t[numedges*stride];
    //for (int i = 0; i < numnodes; i++) omp_init_lock(&nodelocks[i*stride]);
    //for (int i = 0; i < numedges; i++) omp_init_lock(&edgelocks[i*stride]);
	
	for (int i = node_range.x; i <= node_range.y; i++) (*this->node_stiffness) (i) = SYMMETRIC_MATRIX_3X3<T>();

	for (int e = internal_edge_range.x; e <= external_edge_range.y; e++) extended_edge_stiffness (e) = MATRIX_3X3<T>();
    
    int ntets = extended_tetrahedron_range.y - extended_tetrahedron_range.x;
    int bsize = ntets/ntasks;
    int START, END = 0;
    for (int i = 0; i<ntasks; i++)
    {
        START = bsize*i + extended_tetrahedron_range.x; END = START+bsize-1; if (i == ntasks-1) END = extended_tetrahedron_range.y; 
#if defined ENABLE_OMPSS && defined USE_TASKS
        #pragma omp task default(shared) label(UPBS-nested) firstprivate(START,END)
        {
#elif defined ENABLE_OPENMP && defined USE_TASKS
        #pragma omp task default(shared) firstprivate(START,END)
        {
            #ifdef ENABLE_OMPEXTRAE
            OMPSS_POOL& pool = *OMPSS_POOL::Singleton();
            Extrae_eventandcounters(pool.EXTRAEMANUAL,2);
            #endif
#endif
            MATRIX_3X3<T> V_local;
            ARRAYS_2D<MATRIX_3X3<T> > dF_dX_local (1, 4, 1, 4);
            ARRAY<int> vertex (4), edge (6);
            for (int t = START; t<=END; t++)
            {
                int t_parent = extended_tetrahedron_parents (t);
                this->strain_measure.F (t_parent).Fast_Singular_Value_Decomposition (extended_U (t), extended_Fe_hat (t), V_local);
                this->constitutive_model.Isotropic_Stress_Derivative (extended_Fe_hat (t), extended_dP_dFe (t), t_parent);

                if (this->constitutive_model.anisotropic) this->constitutive_model.Update_State_Dependent_Auxiliary_Variables (extended_Fe_hat (t), V_local, t_parent);

                extended_De_inverse_hat (t) = this->strain_measure.Dm_inverse (t_parent) * V_local;
                extended_V (t) = V_local;

                for (int k = 1; k <= 3; k++) for (int l = 1; l <= 3; l++)
                {
                    MATRIX_3X3<T> dDs, dG;
                    dDs (k, l) = (T) 1;

                    if (this->constitutive_model.anisotropic)
                        dG = extended_U (t) * this->constitutive_model.dP_From_dF (extended_U (t).Transposed() * dDs * extended_De_inverse_hat (t),
                            extended_Fe_hat (t), extended_V (t), extended_dP_dFe (t), Be_scales (t_parent), t_parent).
                            Multiply_With_Transpose (extended_De_inverse_hat (t));
                    else dG = extended_U (t) * this->constitutive_model.dP_From_dF (extended_U (t).Transposed() * dDs * extended_De_inverse_hat (t),
                        extended_dP_dFe (t), this->Be_scales (t_parent), t_parent)
                        .Multiply_With_Transpose (extended_De_inverse_hat (t));

                    for (int i = 1; i <= 3; i++) for (int j = 1; j <= 3; j++) dF_dX_local (l + 1, j + 1) (k, i) = dG (i, j);
                }

                for (int i = 2; i <= 4; i++)
                {
                    dF_dX_local (i, 1) = MATRIX_3X3<T>();

                    for (int j = 2; j <= 4; j++) dF_dX_local (i, 1) -= dF_dX_local (i, j);
                }

                for (int j = 1; j <= 4; j++)
                {
                    dF_dX_local (1, j) = MATRIX_3X3<T>();

                    for (int i = 2; i <= 4; i++) dF_dX_local (1, j) -= dF_dX_local (i, j);
                }

                extended_tetrahedrons.Get (t, vertex (1), vertex (2), vertex (3), vertex (4));

                for (int v = 1; v <= 4; v++)
                    if (vertex (v) >= node_range.x && vertex (v) <= node_range.y)
                    {
#ifdef USE_TASKS
                        omp_set_lock(&nodelocks[(vertex (v))*stride]);
#endif
                        (*this->node_stiffness) (vertex (v)) += dF_dX_local (v, v).Symmetric_Part();
#ifdef USE_TASKS
                        omp_unset_lock(&nodelocks[(vertex (v))*stride]);
#endif
                    }

                extended_tetrahedron_extended_edges.Get (t, edge (1), edge (2), edge (3), edge (4), edge (5), edge (6));

                for (int e = 1; e <= 6; e++) if (edge (e))
                {
                    int i, j;
                    extended_edges.Get (edge (e), i, j);
                    int m, n;

                    for (m = 1; m <= 4; m++) if (i == vertex (m)) break;

                    for (n = 1; n <= 4; n++) if (j == vertex (n)) break;

                    assert (m <= 4 && n <= 4 && m != n);
#ifdef USE_TASKS
                    omp_set_lock(&edgelocks[(edge (e))*stride]);
#endif
                    extended_edge_stiffness (edge (e)) += dF_dX_local (m, n);
#ifdef USE_TASKS          
                    omp_unset_lock(&edgelocks[(edge (e))*stride]);
#endif
                }
            }
            #ifdef ENABLE_OMPEXTRAE
            //Extrae_user_function(0);
            Extrae_eventandcounters(pool.EXTRAEMANUAL,0);
            #endif
#if defined USE_TASKS
        }//task
#endif
    }
    #pragma omp taskwait
    //for (int i = 0; i < numnodes; i++) omp_destroy_lock(&nodelocks[i*stride]);
    //for (int i = 0; i < numedges; i++) omp_destroy_lock(&edgelocks[i*stride]);
    //delete[] nodelocks;
    //delete[] edgelocks;
#else //USE_REDUCTION_ROUTINES
	LOG::cerr << "WARNING: using REDUCTION_ROUTINES in Update_Position_Based_State, code not tested ";
    LOG::cerr << "please check source against non-OmpSs version if you need" << std::endl;
    MATRIX_3X3<T> V_local;

	for (int t = this->element_ranges(partition_id).x; t <= this->element_ranges(partition_id).y; t++)
	{
		this->strain_measure.F (t).Fast_Singular_Value_Decomposition (this->U (t), this->Fe_hat (t), V_local);

		if (this->dP_dFe) constitutive_model.Isotropic_Stress_Derivative (this->Fe_hat (t), (*this->dP_dFe) (t), t);

		if (this->dP_dFe && this->constitutive_model.anisotropic)
			this->constitutive_model.Update_State_Dependent_Auxiliary_Variables (this->Fe_hat (t), V_local, t);

		this->De_inverse_hat (t) = this->strain_measure.Dm_inverse (t) * V_local;

		if (V) (*this->V) (t) = V_local;

		if (this->node_stiffness && this->edge_stiffness)
		{
			ARRAYS_2D<MATRIX_3X3<T> > dfdx (1, 4, 1, 4);

			for (int k = 1; k <= 3; k++) for (int l = 1; l <= 3; l++)
				{
					MATRIX_3X3<T> dDs, dG;
					dDs (k, l) = (T) 1;

					if (this->constitutive_model.anisotropic)
						dG = U (t) * this->constitutive_model.dP_From_dF (this->U (t).Transposed() * dDs * this->De_inverse_hat (t),
								this->Fe_hat (t), (*this->V) (t), (*this->dP_dFe) (t),this->Be_scales (t), t)
								.Multiply_With_Transpose (this->De_inverse_hat (t));
					else
						dG = U (t) * this->constitutive_model.dP_From_dF (this->U (t).Transposed() * dDs * this->De_inverse_hat (t),
								(*dP_dFe) (t), this->Be_scales (t), t)
								.Multiply_With_Transpose (this->De_inverse_hat (t));


					for (int i = 1; i <= 3; i++) for (int j = 1; j <= 3; j++) dfdx (l + 1, j + 1) (k, i) = dG (i, j);
				}

			for (int i = 2; i <= 4; i++) for (int j = 2; j <= 4; j++) dfdx (i, 1) -= dfdx (i, j);

			for (int j = 1; j <= 4; j++) for (int i = 2; i <= 4; i++) dfdx (1, j) -= dfdx (i, j);

			ARRAY<int> vertex (4);
			this->strain_measure.tetrahedron_mesh.tetrahedrons.Get (t, vertex (1), vertex (2), vertex (3), vertex (4));

			for (int v = 1; v <= 4; v++)
			{
				this->node_locks.Lock (vertex (v));
				(*this->node_stiffness) (vertex (v)) += dfdx (v, v).Symmetric_Part();
				this->node_locks.Unlock (vertex (v));
			}

			ARRAY<int> edge (6);
			this->strain_measure.tetrahedron_mesh.tetrahedron_edges->Get (t, edge (1), edge (2), edge (3), edge (4), edge (5), edge (6));

			for (int e = 1; e <= 6; e++)
			{
				int i, j;
				this->strain_measure.tetrahedron_mesh.segment_mesh->segments.Get (edge (e), i, j);
				int m, n;

				for (m = 1; m <= 4; m++) if (i == vertex (m)) break;

				for (n = 1; n <= 4; n++) if (j == vertex (n)) break;

				assert (m <= 4 && n <= 4 && m != n);
				this->edge_locks.Lock (edge (e));
				(*edge_stiffness) (edge (e)) += dfdx (m, n);
				this->edge_locks.Unlock (edge (e));
			}
		}
	}
#endif
}

template<class T> void DIAGONALIZED_FINITE_VOLUME_3D<T>::
Update_Position_Based_State()
{
	//OMPSS
	OMPSS_POOL& pool = *OMPSS_POOL::Singleton();

#ifdef USE_REDUCTION_ROUTINES
		THREAD_DIVISION_PARAMETERS<T>& parameters = *THREAD_DIVISION_PARAMETERS<T>::Singleton();
#endif

	LOG::Time ("UPBS (FEM) - Initialize:");
#ifndef USE_REDUCTION_ROUTINES
	int extended_elements = threading_auxiliary_structures->extended_tetrahedrons->m;
	threading_auxiliary_structures->extended_U->Resize_Array (extended_elements);
	threading_auxiliary_structures->extended_De_inverse_hat->Resize_Array (extended_elements);
	threading_auxiliary_structures->extended_Fe_hat->Resize_Array (extended_elements);
	threading_auxiliary_structures->extended_dP_dFe->Resize_Array (extended_elements);
	threading_auxiliary_structures->extended_V->Resize_Array (extended_elements);
	node_stiffness->Resize_Array (strain_measure.tetrahedralized_volume.particles.number);
	threading_auxiliary_structures->extended_edge_stiffness->Resize_Array (threading_auxiliary_structures->extended_edges->m);
#else
	int elements = strain_measure.Dm_inverse.m;
	U.Resize_Array (elements);
	De_inverse_hat.Resize_Array (elements);
	Fe_hat.Resize_Array (elements);

	if (dP_dFe) dP_dFe->Resize_Array (elements);

	if (V) V->Resize_Array (elements);

	if (node_stiffness)
	{
		node_stiffness->Exact_Resize_Array (strain_measure.tetrahedralized_volume.particles.number);
		ARRAY<VECTOR_2D<int> > node_ranges;
		//OMPSS
		parameters.Initialize_Array_Divisions (strain_measure.tetrahedralized_volume.particles.number, pool.Get_n_divisions(), node_ranges);

		ARRAY_PARALLEL_OPERATIONS<SYMMETRIC_MATRIX_3X3<T>, T, VECTOR_3D<T> >::Clear_Parallel (node_stiffness->array, node_ranges);
	}

	if (edge_stiffness)
	{
		edge_stiffness->Exact_Resize_Array (strain_measure.tetrahedron_mesh.segment_mesh->segments.m);
		ARRAY<VECTOR_2D<int> > edge_ranges;

		//OMPSS
		parameters.Initialize_Array_Divisions (strain_measure.tetrahedron_mesh.segment_mesh->segments.m, pool.Get_n_divisions(), edge_ranges);

		ARRAY_PARALLEL_OPERATIONS<MATRIX_3X3<T>, T, VECTOR_3D<T> >::Clear_Parallel (edge_stiffness->array, edge_ranges);
	}
#endif

	LOG::Time ("UPBS (FEM) - Element Loop:");
#ifdef USE_REDUCTION_ROUTINES
	//OMPSS
	parameters.Initialize_Array_Divisions (elements, pool.Get_n_divisions(), element_ranges);
#endif
	#ifndef USE_REDUCTION_ROUTINES
        #ifdef ENABLE_OPENMP
            #ifdef USE_TASKS
                Create_Locks();
                for (unsigned long i = 1; i <= pool.Get_n_divisions(); i++)
                {
                    VECTOR_2D<int>* node_ranges = threading_auxiliary_structures->node_ranges->base_pointer;
                    VECTOR_2D<int>* internal_edge_ranges = threading_auxiliary_structures->internal_edge_ranges->base_pointer;
                    VECTOR_2D<int>* external_edge_ranges = threading_auxiliary_structures->extended_tetrahedron_ranges->base_pointer;
                    #pragma omp task depend(inout:node_ranges[i:1],internal_edge_ranges[i:1],external_edge_ranges[i:1]) \
                                        shared(node_ranges,internal_edge_ranges,external_edge_ranges) firstprivate(i)
                    Update_Position_Based_State_Helper(i);
                }
                //#pragma omp taskwait
            #elif defined USE_WORKSHARING_FOR
                #pragma omp parallel for schedule(static,1)
                for (unsigned long i = 1; i <= pool.Get_n_divisions(); i++)
                    Update_Position_Based_State_Helper(i);
            #endif
        #elif defined ENABLE_OMPSS
            #ifdef USE_TASKS
                Create_Locks();
                for (unsigned long i = 1; i <= pool.Get_n_divisions(); i++)
                {
                    VECTOR_2D<int>& node_range = (*threading_auxiliary_structures->node_ranges) (i);
                    VECTOR_2D<int>& internal_edge_range = (*threading_auxiliary_structures->internal_edge_ranges) (i);
                    VECTOR_2D<int>& external_edge_range = (*threading_auxiliary_structures->extended_tetrahedron_ranges) (i);
                    #pragma omp task inout(node_range,internal_edge_range,external_edge_range) firstprivate(i) label(UPBS)
                    Update_Position_Based_State_Helper(i);
                }
                //#pragma omp taskwait
            #elif defined USE_WORKSHARING_FOR
                #pragma omp for schedule(static,1) label (UPBS-FOR)
                for (unsigned long i = 1; i <= pool.Get_n_divisions(); i++)
                    Update_Position_Based_State_Helper(i);
            #endif //WORKSHARING_FOR
        #endif
	#else //USE_REDUCTION_ROUTINES
        for (unsigned long i = 1; i <= pool.Get_n_divisions(); i++)
        {
            VECTOR_2D<int>& element_range = this->element_ranges (i); 
            #pragma omp task inout(element_range) firstprivate(i) label(UPBS-URR)
            Update_Position_Based_State_Ompss(i);
        }
    #endif

	LOG::Stop_Time();

#ifdef OUTPUT_BENCHMARK_DATA
	unsigned long N_divs = 0;
	//OMPSS
	N_divs = pool.Get_n_divisions();

	FILE_UTILITIES::Write_To_File<T> (STRING_UTILITIES::string_sprintf ("node_stiffness_%d.dat", N_divs), *node_stiffness);
	FILE_UTILITIES::Write_To_File<T> (STRING_UTILITIES::string_sprintf ("extended_edge_stiffness_%d.dat", N_divs), *threading_auxiliary_structures->extended_edge_stiffness);
#endif
}
//#####################################################################
// Function Delete_Position_Based_State
//#####################################################################
template<class T> void DIAGONALIZED_FINITE_VOLUME_3D<T>::
Delete_Position_Based_State()
{
	U.Resize_Array (0);
	De_inverse_hat.Resize_Array (0);
	Fe_hat.Resize_Array (0);

	if (V) V->Resize_Array (0);
}
//#####################################################################
// Function Add_Velocity_Independent_Forces
//#####################################################################
template<class T> void DIAGONALIZED_FINITE_VOLUME_3D<T>::
Add_Velocity_Independent_Forces_Helper (unsigned long partition_id,ARRAY<VECTOR_3D<T> >& F)
{
#ifdef ENABLE_OMPEXTRAE
Extrae_user_function(1);
#endif
#ifndef USE_REDUCTION_ROUTINES
	LIST_ARRAYS<int>const& extended_tetrahedrons = *threading_auxiliary_structures->extended_tetrahedrons;
	VECTOR_2D<int>const& node_range = (*threading_auxiliary_structures->node_ranges)(partition_id);
	VECTOR_2D<int>const& extended_tetrahedron_range = (*threading_auxiliary_structures->extended_tetrahedron_ranges)(partition_id);
	LIST_ARRAY<int>const& extended_tetrahedron_parents = *threading_auxiliary_structures->extended_tetrahedron_parents;
	LIST_ARRAY<MATRIX_3X3<T> >const& extended_U = *threading_auxiliary_structures->extended_U;
	LIST_ARRAY<MATRIX_3X3<T> >const& extended_De_inverse_hat = *threading_auxiliary_structures->extended_De_inverse_hat;
	LIST_ARRAY<DIAGONAL_MATRIX_3X3<T> >const& extended_Fe_hat = *threading_auxiliary_structures->extended_Fe_hat;
	LIST_ARRAY<MATRIX_3X3<T> >const& extended_V = *threading_auxiliary_structures->extended_V;
	if (constitutive_model.anisotropic)
	{
		for (int t = extended_tetrahedron_range.x; t <= extended_tetrahedron_range.y; t++)
		{
			int t_parent = extended_tetrahedron_parents (t);
			MATRIX_3X3<T> forces = extended_U (t) * constitutive_model.P_From_Strain (extended_Fe_hat (t), extended_V (t), Be_scales (t_parent), t_parent).Multiply_With_Transpose (extended_De_inverse_hat (t));
			int node1;
			int node2;
			int node3;
			int node4;
			extended_tetrahedrons.Get (t, node1, node2, node3, node4);

			if (node1 >= node_range.x && node1 <= node_range.y) F (node1) -= VECTOR_3D<T> (forces.x[0] + forces.x[3] + forces.x[6], forces.x[1] + forces.x[4] + forces.x[7], forces.x[2] + forces.x[5] + forces.x[8]);

			if (node2 >= node_range.x && node2 <= node_range.y) F (node2) += VECTOR_3D<T> (forces.x[0], forces.x[1], forces.x[2]);

			if (node3 >= node_range.x && node3 <= node_range.y) F (node3) += VECTOR_3D<T> (forces.x[3], forces.x[4], forces.x[5]);

			if (node4 >= node_range.x && node4 <= node_range.y) F (node4) += VECTOR_3D<T> (forces.x[6], forces.x[7], forces.x[8]);
		}
	}
	else
	{
		for (int t = extended_tetrahedron_range.x; t <= extended_tetrahedron_range.y; t++)
		{
			int t_parent = extended_tetrahedron_parents (t);
			MATRIX_3X3<T> forces = extended_U (t) * constitutive_model.P_From_Strain (extended_Fe_hat (t), Be_scales (t_parent)).Multiply_With_Transpose (extended_De_inverse_hat (t));
			int node1;
			int node2;
			int node3;
			int node4;
			extended_tetrahedrons.Get (t, node1, node2, node3, node4);

			if (node1 >= node_range.x && node1 <= node_range.y) F (node1) -= VECTOR_3D<T> (forces.x[0] + forces.x[3] + forces.x[6], forces.x[1] + forces.x[4] + forces.x[7], forces.x[2] + forces.x[5] + forces.x[8]);

			if (node2 >= node_range.x && node2 <= node_range.y) F (node2) += VECTOR_3D<T> (forces.x[0], forces.x[1], forces.x[2]);

			if (node3 >= node_range.x && node3 <= node_range.y) F (node3) += VECTOR_3D<T> (forces.x[3], forces.x[4], forces.x[5]);

			if (node4 >= node_range.x && node4 <= node_range.y) F (node4) += VECTOR_3D<T> (forces.x[6], forces.x[7], forces.x[8]);
		}
	}

#else
	if (constitutive_model.anisotropic)
	{
		for (int t = element_ranges(partition_id).x; t <= element_ranges(partition_id).y; t++)
		{
			MATRIX_3X3<T> forces = U (t) * constitutive_model.P_From_Strain (Fe_hat (t), (*V) (t), Be_scales (t), t).Multiply_With_Transpose (De_inverse_hat (t));
			int node1;
			int node2;
			int node3;
			int node4;
			strain_measure.tetrahedron_mesh.tetrahedrons.Get (t, node1, node2, node3, node4);
			node_locks.Lock (node1);
			F (node1) -= VECTOR_3D<T> (forces.x[0] + forces.x[3] + forces.x[6], forces.x[1] + forces.x[4] + forces.x[7], forces.x[2] + forces.x[5] + forces.x[8]);
			node_locks.Unlock (node1);
			node_locks.Lock (node2);
			F (node2) += VECTOR_3D<T> (forces.x[0], forces.x[1], forces.x[2]);
			node_locks.Unlock (node2);
			node_locks.Lock (node3);
			F (node3) += VECTOR_3D<T> (forces.x[3], forces.x[4], forces.x[5]);
			node_locks.Unlock (node3);
			node_locks.Lock (node4);
			F (node4) += VECTOR_3D<T> (forces.x[6], forces.x[7], forces.x[8]);
			node_locks.Unlock (node4);
		}
	}
	else
	{
		for (int t = element_ranges(partition_id).x; t <= element_ranges(partition_id).y; t++)
		{
			MATRIX_3X3<T> forces = U (t) * constitutive_model.P_From_Strain (Fe_hat (t), Be_scales (t)).Multiply_With_Transpose (De_inverse_hat (t));
			int node1;
			int node2;
			int node3;
			int node4;
			strain_measure.tetrahedron_mesh.tetrahedrons.Get (t, node1, node2, node3, node4);
			node_locks.Lock (node1);
			F (node1) -= VECTOR_3D<T> (forces.x[0] + forces.x[3] + forces.x[6], forces.x[1] + forces.x[4] + forces.x[7], forces.x[2] + forces.x[5] + forces.x[8]);
			node_locks.Unlock (node1);
			node_locks.Lock (node2);
			F (node2) += VECTOR_3D<T> (forces.x[0], forces.x[1], forces.x[2]);
			node_locks.Unlock (node2);
			node_locks.Lock (node3);
			F (node3) += VECTOR_3D<T> (forces.x[3], forces.x[4], forces.x[5]);
			node_locks.Unlock (node3);
			node_locks.Lock (node4);
			F (node4) += VECTOR_3D<T> (forces.x[6], forces.x[7], forces.x[8]);
			node_locks.Unlock (node4);
		}
	}

#endif
#ifdef ENABLE_OMPEXTRAE
Extrae_user_function(0);
#endif
}
template<class T> void DIAGONALIZED_FINITE_VOLUME_3D<T>::
Add_Velocity_Independent_Forces (ARRAY<VECTOR_3D<T> >& F) const
{
	//OMPSS
	OMPSS_POOL& pool = *OMPSS_POOL::Singleton();
	LOG::Time ("AVIF (FEM):");

#ifdef USE_REDUCTION_ROUTINES
	if (!element_divisions_done)
    {
        THREAD_DIVISION_PARAMETERS<T>& parameters = *THREAD_DIVISION_PARAMETERS<T>::Singleton();
    	parameters.Initialize_Array_Divisions (strain_measure.Dm_inverse.m, pool.Get_n_divisions(), element_ranges);
    }
#endif
    #ifndef USE_REDUCTION_ROUTINES
        #ifdef ENABLE_OPENMP
            #ifdef USE_TASKS
                for (unsigned long i = 1; i <= pool.Get_n_divisions(); i++)
                {
                    VECTOR_2D<int>* node_ranges = threading_auxiliary_structures->node_ranges->base_pointer; 
                    int START = node_ranges[i].x; int END = node_ranges[i].y;
                    VECTOR_3D<T>* F_bp = F.base_pointer;
                    VECTOR_2D<int>* extended_tetrahedron_ranges = threading_auxiliary_structures->extended_tetrahedron_ranges->base_pointer;
                    ARRAY<VECTOR_3D<T> >* dFp = &F; /*So, this is the pointer which allows to respect F*/
                    #pragma omp task depend(in:node_ranges[i:1],extended_tetrahedron_ranges[i:1],dFp[0:1]) \
                                        depend(inout:F_bp[START:END-START+1]) \
                                        firstprivate(i)
                    {
                        ARRAY<VECTOR_3D<T> >& F = *dFp;
                        Add_Velocity_Independent_Forces_Helper(i,F);
                    }
                }
                //#pragma omp taskwait
            #elif defined USE_WORKSHARING_FOR
                #pragma omp parallel for schedule(static,1)
                for (unsigned long i = 1; i <= pool.Get_n_divisions(); i++)
                    Add_Velocity_Independent_Forces_Helper(i,F);
            #endif
        #elif defined ENABLE_OMPSS
            #ifdef USE_TASKS
                for (unsigned long i = 1; i <= pool.Get_n_divisions(); i++)
                {
                    VECTOR_2D<int>& node_range = (*threading_auxiliary_structures->node_ranges) (i); VECTOR_3D<T>& F_start = F(node_range.x);
                    VECTOR_2D<int>& extended_tetrahedron_range = (*threading_auxiliary_structures->extended_tetrahedron_ranges)(i);
                    #pragma omp task in(node_range,extended_tetrahedron_range) inout(F_start) concurrent(F) firstprivate(i) label(AVIF)
                    Add_Velocity_Independent_Forces_Helper(i,F);
                }
                //pragma omp taskwait
            #elif defined USE_WORKSHARING_FOR
                #pragma omp for schedule(static,1) label(AVIF-FOR)
                for (unsigned long i = 1; i <= pool.Get_n_divisions(); i++)
                    Add_Velocity_Independent_Forces_Helper(i,F);
            #endif
        #endif
    #else //USE_REDUCTION_ROUTINES
		VECTOR_2D<int>& element_range = element_ranges (i); VECTOR_3D<T>& F_start = F(this->element_ranges(i).x);
        #pragma omp task in(element_range) inout(F_start) concurrent(F) firstprivate(i) label(AVIF-URR)
        Add_Velocity_Independent_Forces_Ompss(i,F);
    #endif

	LOG::Stop_Time();
}
//#####################################################################
// Function Add_Velocity_Dependent_Forces
//#####################################################################
template<class T> void DIAGONALIZED_FINITE_VOLUME_3D<T>::
Add_Velocity_Dependent_Forces (ARRAY<VECTOR_3D<T> >& F) const
{
	NOT_IMPLEMENTED();
}

//#####################################################################
// Function Add_Force_Differential
//#####################################################################
template<class T> void DIAGONALIZED_FINITE_VOLUME_3D<T>::
Add_Force_Differential_Helper (unsigned long partition_id,ARRAY<VECTOR_3D<T> >const& dX,ARRAY<VECTOR_3D<T> >const& dF)
{
#ifdef ENABLE_OMPEXTRAE
Extrae_user_function(1);
#endif
#ifndef USE_REDUCTION_ROUTINES
	DIAGONALIZED_FINITE_VOLUME_3D_THREADING_AUXILIARY_STRUCTURES<T>const& threading_auxiliary_structures = *this->threading_auxiliary_structures;
	LIST_ARRAYS<int>const& extended_edges = *threading_auxiliary_structures.extended_edges;
	LIST_ARRAY<MATRIX_3X3<T> >const& extended_edge_stiffness = *threading_auxiliary_structures.extended_edge_stiffness;
	VECTOR_2D<int>const& node_range = (*threading_auxiliary_structures.node_ranges) (partition_id);
	VECTOR_2D<int>const& internal_edge_range = (*threading_auxiliary_structures.internal_edge_ranges) (partition_id);
	VECTOR_2D<int>const& external_edge_range = (*threading_auxiliary_structures.external_edge_ranges) (partition_id);

	for (int v = node_range.x; v <= node_range.y; v++) dF (v) += (*this->node_stiffness) (v) * dX (v);

	for (int e = internal_edge_range.x; e <= internal_edge_range.y; e++)
	{
		int m, n;
		extended_edges.Get (e, m, n);
		dF (m) += extended_edge_stiffness (e) * dX (n);
		dF (n) += extended_edge_stiffness (e).Transpose_Times (dX (m));
	}

	for (int e = external_edge_range.x; e <= external_edge_range.y; e++)
	{
		int m, n;
		extended_edges.Get (e, m, n);
		dF (m) += extended_edge_stiffness (e) * dX (n);
	}

#else
	LOG::cerr << "WARNING: using REDUCTION_ROUTINES in Add_Force_Differential, code not tested ";
    LOG::cerr << "please check source against non-OmpSs version if you need" << std::endl;
	STRAIN_MEASURE_3D<T>const& strain_measure = this->strain_measure;
	LIST_ARRAY<SYMMETRIC_MATRIX_3X3<T> >const& node_stiffness = (*this->node_stiffness);
	LIST_ARRAY<MATRIX_3X3<T> >const& edge_stiffness = (*this->edge_stiffness);

	for (int i = this->node_ranges(partition_id).x; i <= this->node_ranges(partition_id).y; i++)
	{
		this->node_locks.Lock (i);
		dF (i) += node_stiffness (i) * dX (i);
		this->node_locks.Unlock (i);
	}

	for (int e = edge_range.x; e <= edge_range.y; e++)
	{
		int m;
		int n;
		strain_measure.tetrahedron_mesh.segment_mesh->segments.Get (e, m, n);
		this->node_locks.Lock (m);
		dF (m) += edge_stiffness (e) * dX (n);
		this->node_locks.Unlock (m);
		this->node_locks.Lock (n);
		dF (n) += edge_stiffness (e).Transpose_Times (dX (m));
		this->node_locks.Unlock (n);
	}

#endif
#ifdef ENABLE_OMPEXTRAE
Extrae_user_function(0);
#endif
}

template<class T> void DIAGONALIZED_FINITE_VOLUME_3D<T>::
Add_Force_Differential (const ARRAY<VECTOR_3D<T> >& dX, ARRAY<VECTOR_3D<T> >& dF) const
{
	//OMPSS
	OMPSS_POOL& pool = *OMPSS_POOL::Singleton();

	LOG::Time ("AFD (FEM):");
#ifdef USE_REDUCTION_ROUTINES
	//OMPSS
	if (!node_edge_divisions_done)
    {
	    THREAD_DIVISION_PARAMETERS<T>& parameters = *THREAD_DIVISION_PARAMETERS<T>::Singleton();
        parameters.Initialize_Array_Divisions (node_stiffness->m, pool.Get_n_divisions(), this->node_ranges);
        parameters.Initialize_Array_Divisions (edge_stiffness->m, pool.Get_n_divisions(), this->edge_ranges);
        this->node_edge_divisions_done = true;
    }
#endif //USE_REDUCTION_ROUTINES
#ifndef USE_REDUCTION_ROUTINES
    #ifdef ENABLE_OPENMP
        #ifdef USE_TASKS
            for (unsigned long i = 1; i <= pool.Get_n_divisions(); i++)
            {
                VECTOR_2D<int>* node_ranges = threading_auxiliary_structures->node_ranges->base_pointer;
                VECTOR_2D<int>* internal_edge_ranges = threading_auxiliary_structures->internal_edge_ranges->base_pointer;
                VECTOR_2D<int>* external_edge_ranges = threading_auxiliary_structures->external_edge_ranges->base_pointer;
                int START = node_ranges[i].x; int END = node_ranges[i].y;
                VECTOR_3D<T>const* dX_bp = dX.base_pointer; VECTOR_3D<T>* dF_bp = dF.base_pointer; ARRAY<VECTOR_3D<T> >* dFp = &dF; ARRAY<VECTOR_3D<T> >* dXp = &dX;
                #pragma omp task depend(in:node_ranges[i:1],internal_edge_ranges[i:1],external_edge_ranges[i:1],dX_bp[START:END-START+1],dX,dFp[0:1]) \
                                depend(inout:dF_bp[START:END-START+1]) \
                                firstprivate(i)
                {
                    const ARRAY<VECTOR_3D<T> >& dX = *dXp; ARRAY<VECTOR_3D<T> >& dF = *dFp;
                    Add_Force_Differential_Helper(i,dX,dF);
                }
            }
            //#pragma omp taskwait
        #elif defined USE_WORKSHARING_FOR
            #pragma omp parallel for schedule(static,1)
            for (unsigned long i = 1; i <= pool.Get_n_divisions(); i++)
                Add_Force_Differential_Helper(i,dX,dF);
        #endif
    #elif defined ENABLE_OMPSS
        #ifdef USE_TASKS
            for (unsigned long i = 1; i <= pool.Get_n_divisions(); i++)
            {
                VECTOR_2D<int>& node_range = (*threading_auxiliary_structures->node_ranges) (i);
                VECTOR_2D<int>& internal_edge_range = (*threading_auxiliary_structures->internal_edge_ranges)(i);
                VECTOR_2D<int>& external_edge_range = (*threading_auxiliary_structures->extended_tetrahedron_ranges)(i);
                VECTOR_3D<T>const& dX_start = dX (node_range.x); VECTOR_3D<T>& dF_start = dF (node_range.x);
                #pragma omp task in(node_range,internal_edge_range,external_edge_range,dX_start,dX) \
                                inout(dF_start) concurrent(dF) firstprivate(i) label(AFD)
                Add_Force_Differential_Helper(i,dX,dF);
            }
            //#pragma omp taskwait
        #elif defined USE_WORKSHARING_FOR
            #pragma omp for schedule(static,1) label(AFD-FOR)
            for (unsigned long i = 1; i <= pool.Get_n_divisions(); i++)
                Add_Force_Differential_Helper(i,dX,dF);
        #endif
    #endif
#else //USE_REDUCTION_ROUTINES
    VECTOR_3D<T>const& dX_start = dX (this->node_ranges(i).x); VECTOR_3D<T>& dF_start = dF (this->node_ranges(i).x);
    #pragma omp task in(node_range,edge_range,dX_start,dX) inout(dF_start) concurrent(dF) firstprivate(i) label(AFD-URR)
    Add_Force_Differential_Helper(i,dX,dF);
#endif //USE_REDUCTION_ROUTINES
	
	LOG::Stop_Time();
}

template<class T> void DIAGONALIZED_FINITE_VOLUME_3D<T>::
Add_Force_Differential (const ARRAY<VECTOR_3D<T> >& dX_full, ARRAY<VECTOR_3D<T> >& dF_full, const int partition_id) const
{
	//LOG::Push_Scope ("AFDS-I:", "AFDS-I:");
	const ARRAY_RANGE<const ARRAY<VECTOR_3D<T> > > dX (dX_full, (*threading_auxiliary_structures->node_ranges) (partition_id));
	ARRAY_RANGE<ARRAY<VECTOR_3D<T> > > dF (dF_full, (*threading_auxiliary_structures->node_ranges) (partition_id));
	const ARRAY_RANGE<const LIST_ARRAY<SYMMETRIC_MATRIX_3X3<T> > > node_stiffness (*this->node_stiffness, (*threading_auxiliary_structures->node_ranges) (partition_id));
	const ARRAYS_RANGE<const LIST_ARRAYS<int> > internal_edges (*threading_auxiliary_structures->extended_edges, (*threading_auxiliary_structures->internal_edge_ranges) (partition_id));
	const ARRAYS_RANGE<const LIST_ARRAYS<int> > external_edges (*threading_auxiliary_structures->extended_edges, (*threading_auxiliary_structures->external_edge_ranges) (partition_id));
	const ARRAY_RANGE<const LIST_ARRAY<MATRIX_3X3<T> > > internal_edge_stiffness (*threading_auxiliary_structures->extended_edge_stiffness, (*threading_auxiliary_structures->internal_edge_ranges) (partition_id));
	const ARRAY_RANGE<const LIST_ARRAY<MATRIX_3X3<T> > > external_edge_stiffness (*threading_auxiliary_structures->extended_edge_stiffness, (*threading_auxiliary_structures->external_edge_ranges) (partition_id));
	for (int p = 1; p <= node_stiffness.m; p++) dF (p) += node_stiffness (p) * dX (p);
	for (int e = 1; e <= internal_edges.m; e++)
	{
		int m, n;
		internal_edges.Get (e, m, n);
		dF_full (m) += internal_edge_stiffness (e) * dX_full (n);
		dF_full (n) += internal_edge_stiffness (e).Transpose_Times (dX_full (m));
	}
	for (int e = 1; e <= external_edges.m; e++)
	{
		int m, n;
		external_edges.Get (e, m, n);
		dF_full (m) += external_edge_stiffness (e) * dX_full (n);
	}
}

template<class T> void DIAGONALIZED_FINITE_VOLUME_3D<T>::
Add_Force_Differential_Internal (const ARRAY<VECTOR_3D<T> >& dX_full, ARRAY<VECTOR_3D<T> >& dF_full, const int partition_id) const
{
	//LOG::Push_Scope ("AFDS-I:", "AFDS-I:");
	const ARRAY_RANGE<const ARRAY<VECTOR_3D<T> > > dX (dX_full, (*threading_auxiliary_structures->node_ranges) (partition_id));
	ARRAY_RANGE<ARRAY<VECTOR_3D<T> > > dF (dF_full, (*threading_auxiliary_structures->node_ranges) (partition_id));
	const ARRAY_RANGE<const LIST_ARRAY<SYMMETRIC_MATRIX_3X3<T> > > node_stiffness (*this->node_stiffness, (*threading_auxiliary_structures->node_ranges) (partition_id));
	const ARRAYS_RANGE<const LIST_ARRAYS<int> > internal_edges (*threading_auxiliary_structures->extended_edges, (*threading_auxiliary_structures->internal_edge_ranges) (partition_id));
	const ARRAY_RANGE<const LIST_ARRAY<MATRIX_3X3<T> > > internal_edge_stiffness (*threading_auxiliary_structures->extended_edge_stiffness, (*threading_auxiliary_structures->internal_edge_ranges) (partition_id));

    //VECTOR_2D<int>& node_ranges = (*threading_auxiliary_structures->node_ranges) (partition_id);
    //VECTOR_2D<int>& internal_edge_ranges = (*threading_auxiliary_structures->internal_edge_ranges) (partition_id);
    //LIST_ARRAYS<int>& internal_edges = (*threading_auxiliary_structures->extended_edges);
    //LIST_ARRAY<MATRIX_3X3<T> >& internal_edge_stiffness = (*threading_auxiliary_structures->extended_edge_stiffness);
    //int MAX, MIN; internal_edges.Get(1,MAX,MIN);
	
    for (int p = 1; p <= dF.m; p++) dF (p) += node_stiffness (p) * dX (p);
     
    for (int e = 1; e <= internal_edges.m; e++)
	{
        int m, n;
        internal_edges.Get (e, m, n);
        dF_full (m) += internal_edge_stiffness (e) * dX_full (n);
        dF_full (n) += internal_edge_stiffness (e).Transpose_Times (dX_full (m));
        //MAX = max(MAX,max(m,n)); MIN = min(MIN,min(m,n));
	}
    //if(MAX > (*threading_auxiliary_structures->node_ranges)(partition_id).y || MIN < (*threading_auxiliary_structures->node_ranges)(partition_id).x)
      //  std::cerr << "AFD_Internal RANGE for Partition <<" << partition_id << ">>, MAX="<< MAX << " MIN=" << MIN << std::endl;
}

template<class T> void DIAGONALIZED_FINITE_VOLUME_3D<T>::
Add_Force_Differential_External (const ARRAY<VECTOR_3D<T> >& dX_full, ARRAY<VECTOR_3D<T> >& dF_full, const int partition_id) const
{
	//LOG::Push_Scope ("AFDS-I:", "AFDS-I:");
	const ARRAYS_RANGE<const LIST_ARRAYS<int> > external_edges (*threading_auxiliary_structures->extended_edges, (*threading_auxiliary_structures->external_edge_ranges) (partition_id));
	const ARRAY_RANGE<const LIST_ARRAY<MATRIX_3X3<T> > > external_edge_stiffness (*threading_auxiliary_structures->extended_edge_stiffness, (*threading_auxiliary_structures->external_edge_ranges) (partition_id));

    //VECTOR_2D<int>& external_edge_ranges = (*threading_auxiliary_structures->external_edge_ranges) (partition_id);
    //LIST_ARRAYS<int>& external_edges = (*threading_auxiliary_structures->extended_edges);
    //LIST_ARRAY<MATRIX_3X3<T> >& external_edge_stiffness = (*threading_auxiliary_structures->extended_edge_stiffness);

    //int MAX, MIN; external_edges.Get(1,MAX,MIN); 
	for (int e = 1; e <= external_edges.m; e++)
	{
		int m, n;
		external_edges.Get (e, m, n);
		dF_full (m) += external_edge_stiffness (e) * dX_full (n);
        //MAX = max(MAX,max(m,n)); MIN = min(MIN,min(m,n));
	}
    //if(MAX > (*threading_auxiliary_structures->node_ranges)(partition_id).y || MIN < (*threading_auxiliary_structures->node_ranges)(partition_id).x)
      //  std::cerr << "AFD_External RANGE for Partition <<" << partition_id << ">>, MAX="<< MAX << " MIN=" << MIN << std::endl;
}

//#####################################################################
// Function Intialize_CFL
//#####################################################################
template<class T> void DIAGONALIZED_FINITE_VOLUME_3D<T>::
Initialize_CFL()
{
	NOT_IMPLEMENTED();
}

//#####################################################################
// Function CFL_Strain_Rate
//#####################################################################
template<class T> T DIAGONALIZED_FINITE_VOLUME_3D<T>::
CFL_Strain_Rate() const
{
	NOT_IMPLEMENTED();
}

//#####################################################################
// Function Semi_Implicit_Step
//#####################################################################
template<class T> void DIAGONALIZED_FINITE_VOLUME_3D<T>::
Semi_Implicit_Impulse_Precomputation (const T time, const T cfl, const T max_dt, ARRAY<T>* time_plus_dt, const bool verbose)
{
	NOT_IMPLEMENTED();
}

//#####################################################################
// Function Semi_Implicit_Recompute_Dt
//#####################################################################
template<class T> void DIAGONALIZED_FINITE_VOLUME_3D<T>::
Semi_Implicit_Recompute_Dt (const int element, T& time_plus_dt)
{
	NOT_IMPLEMENTED();
}

//#####################################################################
// Function Add_Semi_Implicit_Step
//#####################################################################
template<class T> void DIAGONALIZED_FINITE_VOLUME_3D<T>::
Add_Semi_Implicit_Impulse (const int element, const T dt, T* time_plus_dt)
{
	NOT_IMPLEMENTED();
}

//#####################################################################
template class DIAGONALIZED_FINITE_VOLUME_3D<float>;
template class DIAGONALIZED_FINITE_VOLUME_3D<double>;
