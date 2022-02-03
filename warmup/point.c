#include <assert.h>
#include "common.h"
#include "point.h"
#include <math.h>

void point_translate(struct point *p, double x, double y)
{
	//update (x, y) by the input x, y
	p->x += x;
	p->y += y;
}

int point_compare(const struct point *p1, const struct point *p2)
{
	//euclidean distance between p1 and p2
	double dist1;
	double dist2;
	dist1 = sqrt(pow(p1->x, 2) + pow(p1->y, 2));
	dist2 = sqrt(pow(p2->x, 2) + pow(p2->y, 2));
	if (dist1 == dist2){
	       //same length	
		return 0.0;
	}else if (dist1 > dist2){
		//large length
		return 1.0;
	}else{
		//smaller length
		return -1.0;
	}
}

double point_distance(const struct point *p1, const struct point *p2)
{
	//cartesian distance
	return sqrt(pow(p2->x - p1->x, 2) + pow(p2->y - p1->y, 2));
}
