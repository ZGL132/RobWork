#include <rw/math/InertiaMatrix.hpp>
#include <rw/math/Rotation3D.hpp>
#include <array>
using Matrix3=std::array<std::array<double,3>,3>;

extern "C" void newRotate(const rw::math::Rotation3D<double>& r,const double* d,double* out) {
    Matrix3 composite{};
        const auto rotated = r
            * rw::math::InertiaMatrix<double>(d[0], d[1], d[2])
            * rw::math::inverse(r);
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                composite[row][col] += rotated(row, col);
            }
        }

    for(int i=0;i<3;++i) for(int j=0;j<3;++j) out[i*3+j]=composite[i][j];
}
extern "C" void newCuboid(double m,double a,double b,double l,double* out) {
    double ixx,iyy,izz;
            const auto local = rw::math::InertiaMatrix<double>::makeCuboidInertia(
                m, a, b, l);
            ixx = local(0, 0);
            iyy = local(1, 1);
            izz = local(2, 2);
    out[0]=ixx;out[1]=iyy;out[2]=izz;
}
