/* -*- c++ -*-
 * Copyright (c) 2012-2015 by the GalSim developers team on GitHub
 * https://github.com/GalSim-developers
 *
 * This file is part of GalSim: The modular galaxy image simulation toolkit.
 * https://github.com/GalSim-developers/GalSim
 *
 * GalSim is free software: redistribution and use in source and binary forms,
 * with or without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions, and the disclaimer given in the accompanying LICENSE
 *    file.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the disclaimer given in the documentation
 *    and/or other materials provided with the distribution.
 */

// icpc pretends to be GNUC, since it thinks it's compliant, but it's not.
// It doesn't understand "pragma GCC"
#ifndef __INTEL_COMPILER

// For 32-bit machines, g++ -O2 optimization in the TMV stuff below uses an optimization
// that is technically isn't known to not overflow 32 bit integers.  In fact, it is totally
// fine to use, but we need to remove a warning about it in this file for gcc >= 4.5
#if defined(__GNUC__) && __GNUC__ >= 4 && (__GNUC__ >= 5 || __GNUC_MINOR__ >= 5)
#pragma GCC diagnostic ignored "-Wstrict-overflow"
#endif

#endif



#include "TMV.h"
#include "TMV_SymBand.h"
#include "Table.h"
#include <cmath>
#include <vector>

#include <iostream>

namespace galsim {

    // Look up an index.  Use STL binary search; maybe faster to use
    template<class V, class A>
    int Table<V,A>::upperIndex(const A a) const
    {
        if (a<_argMin()-lower_slop || a>_argMax()+upper_slop)
            throw TableOutOfRange(a,_argMin(),_argMax());
        // check for slop
        if (a < v[0].arg) return 1;
        if (a > v[v.size()-1].arg) return v.size()-1;

        // Go directly to index if arguments are regularly spaced.
        if (equalSpaced) {
            int index = int( std::ceil( (a-_argMin()) / dx) );
            if (index >= int(v.size())) --index; // in case of rounding error
            if (index == 0) ++index;
            // check if we need to move ahead or back one step due to rounding errors
            while (a > v[index].arg) ++index;
            while (a < v[index-1].arg) --index;
            return index;
        } else {
            xassert(lastIndex >= 1);
            xassert(lastIndex < int(v.size()));

            if ( a < v[lastIndex-1].arg ) {
                xassert(lastIndex-2 >= 0);
                // Check to see if the previous one is it.
                if (a >= v[lastIndex-2].arg) return --lastIndex;
                else {
                    // Look for the entry from 0..lastIndex-1:
                    Entry e(a,0);
                    iter p = std::upper_bound(v.begin(), v.begin()+lastIndex-1, e);
                    xassert(p != v.begin());
                    xassert(p != v.begin()+lastIndex-1);
                    lastIndex = p-v.begin();
                    return lastIndex;
                }
            } else if (a > v[lastIndex].arg) {
                xassert(lastIndex+1 < int(v.size()));
                // Check to see if the next one is it.
                if (a <= v[lastIndex+1].arg) return ++lastIndex;
                else {
                    // Look for the entry from lastIndex..end
                    Entry e(a,0);
                    iter p = std::lower_bound(v.begin()+lastIndex+1, v.end(), e);
                    xassert(p != v.begin()+lastIndex+1);
                    xassert(p != v.end());
                    lastIndex = p-v.begin();
                    return lastIndex;
                }
            } else {
                // Then lastIndex is correct.
                return lastIndex;
            }
        }
    }

    //new element for table.
    template<class V, class A>
    void Table<V,A>::addEntry(const A _arg, const V _val)
    {
        Entry e(_arg,_val);
        v.push_back(e);
        isReady = false; //re-sort array next time used
    }

    template<class V, class A>
    Table<V,A>::Table(const A* argvec, const V* valvec, int N, interpolant in) :
        iType(in), isReady(false)
    {
        v.reserve(N);
        const A* aptr;
        const V* vptr;
        int i;
        for (i=0, aptr=argvec, vptr=valvec; i<N; i++, aptr++, vptr++) {
            Entry e(*aptr,*vptr);
            v.push_back(e);
        }
    }

    template<class V, class A>
    Table<V,A>::Table(const std::vector<A>& aa, const std::vector<V>& vv, interpolant in) :
        iType(in), isReady(false)
    {
        v.reserve(aa.size());
        if (vv.size() != aa.size())
            throw TableError("input vector lengths don't match");
        if (iType == spline && vv.size() < 3)
            throw TableError("input vectors are too short to spline interpolate");
        if (vv.size() < 2 &&  (iType == linear || iType == ceil || iType == floor))
            throw TableError("input vectors are too short for interpolation");
        typename std::vector<A>::const_iterator aptr=aa.begin();
        typename std::vector<V>::const_iterator vptr=vv.begin();
        for (size_t i=0; i<aa.size(); i++, ++aptr, ++vptr) {
            Entry e(*aptr,*vptr);
            v.push_back(e);
        }
    }

    //lookup & interp. function value. - this one returns 0 out of bounds.
    template<class V, class A>
    V Table<V,A>::operator() (const A a) const
    {
        setup(); //do any necessary prep
        if (a<_argMin() || a>_argMax()) return V(0);
        else {
            int i = upperIndex(a);
            return interpolate(a,i,v,y2);
        }
    }

    //lookup & interp. function value.
    template<class V, class A>
    V Table<V,A>::lookup(const A a) const
    {
        setup();
        int i = upperIndex(a);
        return interpolate(a,i,v,y2);
    }

    template <class V, class A>
    void Table<V,A>::interpMany(const A* argvec, V* valvec, int N) const
    {
        setup();
        for (int k=0; k<N; ++k) {
            int i = upperIndex(argvec[k]);
            valvec[k] = interpolate(argvec[k],i,v,y2);
        }
    }

    template<class V, class A>
    V Table<V,A>::linearInterpolate(
        A a, int i, const std::vector<Entry>& v, const std::vector<V>& )
    {
        A h = v[i].arg - v[i-1].arg;
        A aa = (v[i].arg - a) / h;
        A bb = 1. - aa;
        return aa*v[i-1].val + bb*v[i].val;
    }

    template<class V, class A>
    V Table<V,A>::splineInterpolate(
        A a, int i, const std::vector<Entry>& v, const std::vector<V>& y2)
    {
#if 0
        // Direct calculation saved for comparison:
        A h = v[i].arg - v[i-1].arg;
        A aa = (v[i].arg - a)/h;
        A bb = 1. - aa;
        return aa*v[i-1].val +bb*v[i].val +
            ((aa*aa*aa-aa)*y2[i-1]+(bb*bb*bb-bb)*y2[i]) *
            (h*h)/6.0;
#else
        // Factor out h factors, so only need 1 division by h.
        // Also, use the fact that bb = h-aa to simplify the calculation.
        A h = v[i].arg - v[i-1].arg;
        A aa = (v[i].arg - a);
        A bb = h-aa;
        return ( aa*v[i-1].val + bb*v[i].val -
                 (1./6.) * aa * bb * ( (aa+h)*y2[i-1] +
                                       (bb+h)*y2[i]) ) / h;
#endif
    }

    template<class V, class A>
    V Table<V,A>::floorInterpolate(
        A a, int i, const std::vector<Entry>& v, const std::vector<V>& )
    {
        // On entry, it is only guaranteed that v[i-1].arg <= a <= v[i].arg.
        // Normally those ='s are ok, but for floor and ceil we make the extra
        // check to see if we should choose the opposite bound.
        if (v[i].arg == a) return v[i].val;
        else return v[i-1].val;
    }

    template<class V, class A>
    V Table<V,A>::ceilInterpolate(
        A a, int i, const std::vector<Entry>& v, const std::vector<V>& )
    {
        if (v[i-1].arg == a) return v[i-1].val;
        return v[i].val;
    }

    template<class V, class A>
    void Table<V,A>::read(std::istream& is)
    {
        std::string line;
        const std::string comments="#;!"; //starts comment
        V vv;
        A aa;
        while (is) {
            getline(is,line);
            // skip leading white space:
            size_t i;
            for (i=0;  isspace(line[i]) && i<line.length(); i++) ;
            // skip line if blank or just comment
            if (i==line.length()) continue;
            if (comments.find(line[i])!=std::string::npos) continue;
            // try reading arg & val from line:
            std::istringstream iss(line);
            iss >> aa >> vv;
            if (iss.fail()) throw TableReadError(line) ;
            addEntry(aa,vv);
        }
    }

    // Do any necessary setup of the table before using
    template<class V, class A>
    void Table<V,A>::setup() const
    {
        if (isReady) return;

        if (v.size() <= 1)
            throw TableError("Trying to use a null Table (need at least 2 entries)");

        sortIt();
        lastIndex = 1; // Start back at the beginning for the next search.

        // See if arguments are equally spaced
        // ...within this fractional error:
        const double tolerance = 0.01;
        dx = (v.back().arg - v.front().arg) / (v.size()-1);
        if (dx == 0.)
            throw TableError("First and last Table entry are equal.");
        equalSpaced = true;
        for (int i=1; i<int(v.size()); i++) {
            if ( std::abs( ((v[i].arg-v[0].arg)/dx - i)) > tolerance) equalSpaced = false;
            if (v[i].arg == v[i-1].arg)
                throw TableError("Table has repeated arguments.");
        }

        switch (iType) {
          case linear:
               interpolate = &Table<V,A>::linearInterpolate;
               break;
          case spline :
               setupSpline();
               interpolate = &Table<V,A>::splineInterpolate;
               break;
          case floor:
               interpolate = &Table<V,A>::floorInterpolate;
               break;
          case ceil:
               interpolate = &Table<V,A>::ceilInterpolate;
               break;
          default:
               throw TableError("interpolation method not yet implemented");
        }

        lower_slop = (v[1].arg - v[0].arg) * 1.e-6;
        upper_slop = (v[v.size()-1].arg - v[v.size()-2].arg) * 1.e-6;

        isReady = true;
    }

    template <class V, class A>
    void Table<V,A>::setupSpline() const
    {
        /**
         * Calculate the 2nd derivatives of the natural cubic spline.
         *
         * Here we follow the broad procedure outlined in this technical note by Jim
         * Armstrong, freely available online:
         * http://www.algorithmist.net/spline.html
         *
         * The system we solve is equation [7].  In our adopted notation u_i are the diagonals
         * of the matrix M, and h_i the off-diagonals.  y'' is z_i and the rhs = v_i.
         *
         * For table sizes larger than the fully trivial (2 or 3 elements), we use the
         * symmetric tridiagonal matrix solution capabilities of MJ's TMV library.
         */
        // Set up the 2nd-derivative table for splines
        int n = v.size();
        y2.resize(n);
        // End points 2nd-derivatives zero for natural cubic spline
        y2[0] = V(0);
        y2[n-1] = V(0);
        // For 3 points second derivative at i=1 is simple
        if (n == 3){

            y2[1] = 3.*((v[2].val - v[1].val) / (v[2].arg - v[1].arg) -
                        (v[1].val - v[0].val) / (v[1].arg - v[0].arg)) / (v[2].arg - v[0].arg);

        } else {  // For 4 or more points we use the TMV symmetric tridiagonal matrix solver

            tmv::SymBandMatrix<V> M(n-2, 1);
            for (int i=1; i<=n-3; i++){
                M(i, i-1) = v[i+1].arg - v[i].arg;
            }
            tmv::Vector<V> rhs(n-2);
            for (int i=1; i<=n-2; i++){
                M(i-1, i-1) = 2. * (v[i+1].arg - v[i-1].arg);
                rhs(i-1) = 6. * ( (v[i+1].val - v[i].val) / (v[i+1].arg - v[i].arg) -
                                  (v[i].val - v[i-1].val) / (v[i].arg - v[i-1].arg) );
            }
            tmv::Vector<V> solution(n-2);
            solution = rhs / M;   // solve the tridiagonal system of equations
            for (int i=1; i<=n-2; i++){
                y2[i] = solution[i-1];
            }
        }
    }

    template class Table<double,double>;

    // Start Table2D

    // Should dx, dy be higher precision types than x0, y0?  Maybe x0, xend would be better params?
    template<class V, class A>
    Table2D<V,A>::Table2D(A _x0, A _y0, A _dx, A _dy, int _Nx, int _Ny, const V* valarray,
        interpolant in) : iType(in), Nx(_Nx), Ny(_Ny), x0(_x0), y0(_y0), dx(_dx), dy(_dy),
                          xmax(x0+(Nx-1)*dx), ymax(y0+(Ny-1)*dy), xslop(dx*1e-6), yslop(1e-6)
    {
        // Allocate vectors.
        vals.reserve(Nx*Ny);
        xgrid.reserve(Nx);
        ygrid.reserve(Ny);

        // Fill in vectors.
        const V* vptr;
        int i;
        for (i=0, vptr=valarray; i<Nx*Ny; i++, vptr++) {
            vals.push_back(*vptr);
        }
        for (i=0; i<Nx; i++) xgrid.push_back(x0+dx*i);
        for (i=0; i<Ny; i++) ygrid.push_back(y0+dy*i);

        equalSpaced = true;

        // Map specific interpolator to `interpolate`.
        switch (iType) {
          case linear:
               interpolate = &Table2D<V,A>::linearInterpolate;
               break;
          case floor:
               interpolate = &Table2D<V,A>::floorInterpolate;
               break;
          case ceil:
               interpolate = &Table2D<V,A>::ceilInterpolate;
               break;
          default:
               throw TableError("interpolation method not yet implemented");
        }
    }

    template<class V, class A>
    int Table2D<V,A>::upperIndexX(A x) const
    {
        if (x<_xArgMin()-xslop || x>_xArgMax()+xslop)
            throw TableOutOfRange(x,_xArgMin(),_xArgMax());
        // check for slop
        if (x < _xArgMin()) return 1;
        if (x > _xArgMax()) return xgrid.size()-1;

        if (equalSpaced) {
            int i = int( std::ceil( (x-_xArgMin()) / dx) );
            if (i >= int(xgrid.size())) --i; // in case of rounding error
            if (i == 0) ++i;
            // check if we need to move ahead or back one step due to rounding errors
            while (x > xgrid[i]) ++i;
            while (x < xgrid[i-1]) --i;
            return i;
        }
    }

    template<class V, class A>
    int Table2D<V,A>::upperIndexY(A y) const
    {
        if (y<_yArgMin()-yslop || y>_yArgMax()+yslop)
            throw TableOutOfRange(y,_yArgMin(),_yArgMax());
        // check for slop
        if (y < _yArgMin()) y=ygrid.front();
        if (y > _yArgMax()) y=ygrid.back();

        if (equalSpaced) {
            int j = int( std::ceil( (y-_yArgMin()) / dy) );
            if (j >= int(ygrid.size())) --j; // in case of rounding error
            if (j == 0) ++j;
            // check if we need to move ahead or back one step due to rounding errors
            while (y > ygrid[j]) ++j;
            while (y < ygrid[j-1]) --j;
            return j;
        }
    }

    //lookup and interpolate function value.
    template<class V, class A>
    V Table2D<V,A>::lookup(const A x, const A y) const
    {
        int i = upperIndexX(x);
        int j = upperIndexY(y);
        return interpolate(x, y, xgrid[i], ygrid[j], dx, dy, i, j, vals, Nx);
    }

    template<class V, class A>
    void Table2D<V,A>::interpManyScatter(const A* xvec, const A* yvec, V* valvec, int N) const
    {
        int i, j;
        for (int k=0; k<N; k++) {
            i = upperIndexX(xvec[k]);
            j = upperIndexY(yvec[k]);
            valvec[k] = interpolate(xvec[k], yvec[k], xgrid[i], ygrid[j], dx, dy, i, j, vals, Nx);
        }
    }

    template<class V, class A>
    void Table2D<V,A>::interpManyOuter(const A* xvec, const A* yvec, V* valvec,
                                       int outNx, int outNy) const
    {
        int i, j;
        for (int outj=0; outj<outNy; outj++) {
            j = upperIndexY(yvec[outj]);
            for (int outi=0; outi<outNx; outi++, valvec++) {
                i = upperIndexX(xvec[outi]);
                *valvec = interpolate(xvec[outi], yvec[outj], xgrid[i], ygrid[j], dx, dy, i, j, vals, Nx);
            }
        }
    }

    template<class V, class A>
    V Table2D<V,A>::linearInterpolate(A x, A y, A xi, A yj, A dx, A dy, int i, int j,
        const std::vector<V>& vals, int Nx)
    {
        A ax = (xi - x) / dx;
        A bx = 1.0 - ax;
        A ay = (yj - y) / dy;
        A by = 1.0 - ay;
        return (vals[(j-1)*Nx+i-1] * ax * ay
                + vals[j*Nx+i-1] * ax * by
                + vals[(j-1)*Nx+i] * bx * ay
                + vals[j*Nx+i] * bx * by);
    }

    template<class V, class A>
    V Table2D<V,A>::floorInterpolate(A x, A y, A xi, A yj, A dx, A dy, int i, int j,
        const std::vector<V>& vals, int Nx)
    {
        if (x == xi) i++;
        if (y == yj) j++;
        return vals[(j-1)*Nx+i-1];
    }

    template<class V, class A>
    V Table2D<V,A>::ceilInterpolate(A x, A y, A xi, A yj, A dx, A dy, int i, int j,
        const std::vector<V>& vals, int Nx)
    {
        if (x == xi) i++;
        if (y == yj) j++;
        return vals[j*Nx+i];
    }

    template class Table2D<double,double>;
}
