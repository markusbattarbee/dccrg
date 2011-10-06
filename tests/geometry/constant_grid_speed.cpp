/*
Tests the speed of geometry operations with a constant cell size grid
*/

#include "ctime"
#include "iostream"
#include "stdint.h"

#include "../../dccrg_geometry.hpp"

using namespace std;
using namespace dccrg;

int main(void)
{
	clock_t before, after;

	cout << "\nConstant grid:" << endl;

	#define CELLS 100000000
	Geometry geometry;
	geometry.set_x_start(0);
	geometry.set_y_start(0);
	geometry.set_z_start(0);
	geometry.set_cell_x_size(1);
	geometry.set_cell_y_size(1.1);
	geometry.set_cell_z_size(1.2);
	geometry.set_x_length(100);
	geometry.set_y_length(200);
	geometry.set_z_length(300);
	geometry.set_maximum_refinement_level(geometry.get_maximum_possible_refinement_level());
	cout << "\tMaximum refinement level: " << geometry.get_maximum_refinement_level() << endl;

	double avg_size = 0;
	before = clock();
	for (uint64_t cell = 1; cell <= CELLS; cell++) {
		avg_size += geometry.get_cell_x_size(cell);
	}
	after = clock();
	avg_size /= CELLS;
	cout << "\tAverage cell x size: " << avg_size;
	cout << ", time for " << double(CELLS) << " cells: " << double(after - before) / CLOCKS_PER_SEC << " s" << endl;

	avg_size = 0;
	before = clock();
	for (uint64_t cell = 1; cell <= CELLS; cell++) {
		avg_size += geometry.get_cell_y_size(cell);
	}
	after = clock();
	avg_size /= CELLS;
	cout << "\tAverage cell y size: " << avg_size;
	cout << ", time for " << double(CELLS) << " cells: " << double(after - before) / CLOCKS_PER_SEC << " s" << endl;

	avg_size = 0;
	before = clock();
	for (uint64_t cell = 1; cell <= CELLS; cell++) {
		avg_size += geometry.get_cell_z_size(cell);
	}
	after = clock();
	avg_size /= CELLS;
	cout << "\tAverage cell z size: " << avg_size;
	cout << ", time for " << double(CELLS) << " cells: " << double(after - before) / CLOCKS_PER_SEC << " s" << endl;



	double avg_pos = 0;
	before = clock();
	for (uint64_t cell = 1; cell <= CELLS; cell++) {
		avg_pos += geometry.get_cell_x(cell);
	}
	after = clock();
	avg_pos /= CELLS;
	cout << "\tAverage cell x position: " << avg_pos;
	cout << ", time for " << double(CELLS) << " cells: " << double(after - before) / CLOCKS_PER_SEC << " s" << endl;

	avg_pos = 0;
	before = clock();
	for (uint64_t cell = 1; cell <= CELLS; cell++) {
		avg_pos += geometry.get_cell_y(cell);
	}
	after = clock();
	avg_pos /= CELLS;
	cout << "\tAverage cell y position: " << avg_pos;
	cout << ", time for " << double(CELLS) << " cells: " << double(after - before) / CLOCKS_PER_SEC << " s" << endl;

	avg_pos = 0;
	before = clock();
	for (uint64_t cell = 1; cell <= CELLS; cell++) {
		avg_pos += geometry.get_cell_z(cell);
	}
	after = clock();
	avg_pos /= CELLS;
	cout << "\tAverage cell z position: " << avg_pos;
	cout << ", time for " << double(CELLS) << " cells: " << double(after - before) / CLOCKS_PER_SEC << " s" << endl;

	return 0;
}
