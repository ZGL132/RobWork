#include <rw/math/InertiaMatrix.hpp>
#include <rw/math/Rotation3D.hpp>
#include <array>
using Matrix3=std::array<std::array<double,3>,3>;
void accumulateRotatedInertia(Matrix3& m, const rw::math::Rotation3D<double>& r,
                              double ixx, double iyy, double izz)
{
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            m[row][col] += r(row, 0) * ixx * r(col, 0)
                         + r(row, 1) * iyy * r(col, 1)
                         + r(row, 2) * izz * r(col, 2);
        }
    }
}

extern "C" void oldRotate(const rw::math::Rotation3D<double>& r,const double* d,double* out) {
    Matrix3 m{};accumulateRotatedInertia(m,r,d[0],d[1],d[2]);
    for(int i=0;i<3;++i) for(int j=0;j<3;++j) out[i*3+j]=m[i][j];
}
extern "C" void oldCuboid(double m,double a,double b,double l,double* out) {
    double ixx,iyy,izz;
            ixx = m * (b * b + l * l) / 12.0;
            iyy = m * (a * a + l * l) / 12.0;
            izz = m * (a * a + b * b) / 12.0;
    out[0]=ixx;out[1]=iyy;out[2]=izz;
}
