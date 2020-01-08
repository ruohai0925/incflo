#include "incflo_godunov_ppm.H" 
#include "incflo.H"
#include <AMReX_BCRec.H>
#include <iomanip> 

using namespace amrex;

void
incflo::incflo_predict_godunov ( int lev, Real time,
                                 Vector< std::unique_ptr<MultiFab> >& vel_in)
{
    BL_PROFILE("incflo::incflo_predict_godunov");

    Box domain(geom[lev].Domain());

    // These are place-holders for now
    MultiFab tforces(grids[lev], dmap[lev], 3, 2);
    MultiFab divu   (grids[lev], dmap[lev], 1, 2);
    tforces.setVal(0.0);
    divu.setVal(0.0);

    BCRec dom_bc;
    {
      // const int* lo_bc = phys_bc.lo();
      // const int* hi_bc = phys_bc.hi();
      // HACK -- just set to all int_dir as stand-in for periodic
      dom_bc.setLo(0,BCType::int_dir);
      dom_bc.setHi(0,BCType::int_dir);
      dom_bc.setLo(1,BCType::int_dir);
      dom_bc.setHi(1,BCType::int_dir);
      dom_bc.setLo(2,BCType::int_dir);
      dom_bc.setHi(2,BCType::int_dir);
    }

    for (MFIter mfi(*vel_in[lev],TilingIfNotGPU()); mfi.isValid(); ++mfi)
    {
        // Tilebox
        Box bx = mfi.tilebox();

        Gpu::ManagedVector<BCRec> bc(3);
        for (int n = 0; n < 3; ++n)
            setBC(bx, geom[lev].Domain(), dom_bc, bc[n]);

        incflo_predict_godunov_on_box(lev, bx, (*vel_in[lev]).array(mfi), 
                                      m_u_mac[lev]->array(mfi), m_v_mac[lev]->array(mfi), m_w_mac[lev]->array(mfi),
                                      tforces.array(mfi), divu.array(mfi), bc);

    }
}

void
incflo::incflo_predict_godunov_on_box (const int lev, Box& bx,
                                       const Array4<Real> &a_vel,
                                       const Array4<const Real> &u_face, 
                                       const Array4<const Real> &v_face, 
                                       const Array4<const Real> &w_face,
                                       const Array4<Real> &tf, 
                                       const Array4<Real> &divu_cc,
                                       const Gpu::ManagedVector<BCRec> &BCs)
{
    Box domain(geom[lev].Domain());

    int ncomp = 1;

    int iconserv[3];
    for (int i = 0; i < 3; i++) iconserv[i] = 1;

    auto const g2bx = amrex::grow(bx, 2); 
    auto const g1bx = amrex::grow(bx, 1); 

    const Real dx = geom[lev].CellSize(0); 
    const Real dy = geom[lev].CellSize(1); 
    const Real dz = geom[lev].CellSize(2); 
   
    auto const gxbx = amrex::grow(g2bx,0, 1); 
    FArrayBox Imxf(gxbx, ncomp); 
    FArrayBox Ipxf(gxbx, ncomp);
    Elixir Imxeli = Imxf.elixir(); 
    Elixir Ipxeli = Ipxf.elixir(); 
    auto const Imx = Imxf.array(); 
    auto const Ipx = Ipxf.array(); 

    auto const gybx = amrex::grow(g2bx,1, 1); 
    FArrayBox Imyf(gybx, ncomp); 
    FArrayBox Ipyf(gybx, ncomp);
    Elixir Imyeli = Imyf.elixir(); 
    Elixir Ipyeli = Ipyf.elixir(); 
    auto const Imy = Imyf.array(); 
    auto const Ipy = Ipyf.array(); 
   
    auto const gzbx = amrex::grow(g2bx,2, 1); 
    FArrayBox Imzf(gzbx, ncomp); 
    FArrayBox Ipzf(gzbx, ncomp);
    Elixir Imzeli = Imzf.elixir(); 
    Elixir Ipzeli = Ipzf.elixir(); 
    auto const Imz = Imzf.array(); 
    auto const Ipz = Ipzf.array(); 

    /* Temporary Edge States */ 
    FArrayBox xedgef(g2bx, ncomp); 
    FArrayBox yedgef(g2bx, ncomp); 
    FArrayBox zedgef(g2bx, ncomp); 

    Elixir xedeli = xedgef.elixir(); 
    Elixir yedeli = yedgef.elixir(); 
    Elixir zedeli = zedgef.elixir(); 

    auto const xedge = xedgef.array(); 
    auto const yedge = yedgef.array(); 
    auto const zedge = zedgef.array(); 

    auto const xgbx = surroundingNodes(g1bx, 0);
    auto const ygbx = surroundingNodes(g1bx, 1); 
    auto const zgbx = surroundingNodes(g1bx, 2); 

    /* Use PPM to generate Im and Ip */
 
    AMREX_PARALLEL_FOR_4D (g1bx, ncomp, i, j, k, n, {
        const auto bc = BCs[n];
        Godunov_ppm_pred(i, j, k, n, dt, dx, a_vel, a_vel, Imx, Ipx, bc, bx.loVect()[0], bx.hiVect()[0], 0);
    });

    AMREX_PARALLEL_FOR_4D (g1bx, ncomp, i, j, k, n, {
        const auto bc = BCs[n];
        Godunov_ppm_pred(i, j, k, n, dt, dy, a_vel, a_vel, Imy, Ipy, bc, bx.loVect()[1], bx.hiVect()[1], 1);
    });

    AMREX_PARALLEL_FOR_4D (g1bx, ncomp, i, j, k, n, {
        const auto bc = BCs[n];
        Godunov_ppm_pred(i, j, k, n, dt, dz, a_vel, a_vel, Imz, Ipz, bc, bx.loVect()[2], bx.hiVect()[2], 2);
    }); 

    FArrayBox xlf(xgbx, ncomp); 
    FArrayBox xhf(xgbx, ncomp);
    Elixir xleli = xlf.elixir(); 
    Elixir xheli = xhf.elixir(); 
    auto const xlo = xlf.array(); 
    auto const xhi = xhf.array(); 

    FArrayBox ylf(ygbx, ncomp); 
    FArrayBox yhf(ygbx, ncomp);
    Elixir yleli = ylf.elixir(); 
    Elixir yheli = yhf.elixir(); 
    auto const ylo = ylf.array(); 
    auto const yhi = yhf.array(); 
   
    FArrayBox zlf(zgbx, ncomp); 
    FArrayBox zhf(zgbx, ncomp);
    Elixir zleli = zlf.elixir(); 
    Elixir zheli = zhf.elixir(); 
    auto const zlo = zlf.array(); 
    auto const zhi = zhf.array();

    auto const txbx = surroundingNodes(grow(g1bx, 0, -1), 0);
    auto const tybx = surroundingNodes(grow(g1bx, 1, -1), 1); 
    auto const tzbx = surroundingNodes(grow(g1bx, 2, -1), 2);
    
#if 0
// --------------------X -------------------------------------------------
    AMREX_PARALLEL_FOR_4D(txbx, ncomp, i, j, k, n, {
        Real cons1;
        Real cons2; 
        Real lo; 
        Real hi; 
        Real st;
        Real fux = (std::abs(u_mac(i,j,k)) < 1e-06)? 0.e0 : 1.e0; 
        bool uval = u_mac(i,j,k) >= 0.e0; 
        auto bc = BCs[n];  
        cons1 = (iconserv[n]==1)? - 0.5e0*dt*s(i-1,j,k,n)*divu_cc(i-1,j,k) : 0;
        cons2 = (iconserv[n]==1)? - 0.5e0*dt*s(i  ,j,k,n)*divu_cc(i  ,j,k) : 0;
        lo    = Ipx(i-1,j,k,n) + 0.5e0*dt*tf(i-1,j,k,n) + cons1; 
        hi    = Imx(i  ,j,k,n) + 0.5e0*dt*tf(i  ,j,k,n) + cons2;
        Godunov_trans_xbc(i, j, k, n, s, lo, hi, u_mac, bc.lo(0), bc.hi(0), 
                            bx.loVect(), bx.hiVect(), false, false);  
        xlo(i,j,k,n) = lo; 
        xhi(i,j,k,n) = hi;
        st = (uval) ? lo : hi;
        xedge(i, j, k, n) = fux*st + (1.e0 - fux)*0.5e0*(hi + lo);
    }); 

// Clear integral states from PPM 
    Imxeli.clear(); 
    Ipxeli.clear(); 

//--------------------Y -------------------------------------------------
     AMREX_PARALLEL_FOR_4D(tybx, ncomp, i, j, k, n, {
        Real cons1;
        Real cons2; 
        Real lo; 
        Real hi; 
        Real st;
        Real fuy = (std::abs(v_mac(i,j,k)) < 1e-06)? 0.e0 : 1.e0; 

        bool vval = v_mac(i,j,k) >= 0.e0; 
        auto bc = BCs[n];  
        cons1 = (iconserv[n]==1)? - 0.5e0*dt*s(i,j-1,k,n)*divu_cc(i,j-1,k) : 0;
        cons2 = (iconserv[n]==1)? - 0.5e0*dt*s(i,j  ,k,n)*divu_cc(i,j  ,k) : 0;
        lo    = Ipy(i,j-1,k,n) + 0.5e0*dt*tf(i,j-1,k,n) + cons1; 
        hi    = Imy(i,j,k,n)   + 0.5e0*dt*tf(i,j,k,n) + cons2; 
        Godunov_trans_ybc(i, j, k, n, s, lo, hi, v_mac, bc.lo(1), bc.hi(1),
                                 bx.loVect(), bx.hiVect(), false, false);  
        ylo(i,j,k,n) = lo; 
        yhi(i,j,k,n) = hi; 
        st    = (vval >= 0.e0) ? lo : hi; 
        yedge(i, j, k, n) = fuy*st + (1.e0 - fuy)*0.5e0*(hi + lo);
    });

// Clear integral states from PPM 
    Imyeli.clear(); 
    Ipyeli.clear(); 

//------------------ Z ---------------------------------------------------
     AMREX_PARALLEL_FOR_4D(tzbx, ncomp, i, j, k, n, {
        Real cons1;
        Real cons2; 
        Real lo; 
        Real hi; 
        Real st;
        Real fuz = (std::abs(w_mac(i,j,k)) < 1e-06)? 0.e0 : 1.e0; 
        bool wval = w_mac(i,j,k) >= 0.e0; 
        auto bc = BCs[n];  
        cons1 = (iconserv[n]==1)? - 0.5e0*dt*s(i,j,k-1,n)*divu_cc(i,j,k-1) : 0;
        cons2 = (iconserv[n]==1)? - 0.5e0*dt*s(i,j,k  ,n)*divu_cc(i,j,k  ) : 0;
        lo    = Ipz(i,j,k-1,n) + 0.5e0*dt*tf(i,j,k-1,n) + cons1; 
        hi    = Imz(i,j,k,n)   + 0.5e0*dt*tf(i,j,k,n) + cons2; 
        Godunov_trans_zbc(i, j, k, n, s, lo, hi, w_mac, bc.lo(2), bc.hi(2),
                                 bx.loVect(), bx.hiVect(), false, false); 

        zlo(i,j,k,n) = lo; 
        zhi(i,j,k,n) = hi; 
        st    = (wval) ? lo : hi;
        zedge(i, j, k, n) = fuz*st + (1.e0 - fuz)*0.5e0*(hi + lo);

    });     

// Clear integral states from PPM 
    Imzeli.clear(); 
    Ipzeli.clear(); 
 

//-----------------Create temporary fabs for corner/transverse 

    //X fabs
    FArrayBox xylof(g1bx, ncomp); 
    FArrayBox xyhif(g1bx, ncomp); 
    FArrayBox xzlof(g1bx, ncomp); 
    FArrayBox xzhif(g1bx, ncomp); 
    Elixir xyle = xylof.elixir(); 
    Elixir xyhe = xyhif.elixir(); 
    Elixir xzle = xzlof.elixir(); 
    Elixir xzhe = xzhif.elixir(); 
    const auto xylo = xylof.array(); 
    const auto xyhi = xyhif.array(); 
    const auto xzlo = xzlof.array(); 
    const auto xzhi = xzhif.array(); 

    //Y fabs
    FArrayBox yxlof(g1bx, ncomp); 
    FArrayBox yxhif(g1bx, ncomp); 
    FArrayBox yzlof(g1bx, ncomp); 
    FArrayBox yzhif(g1bx, ncomp); 
    Elixir yxle = yxlof.elixir(); 
    Elixir yxhe = yxhif.elixir(); 
    Elixir yzle = yzlof.elixir(); 
    Elixir yzhe = yzhif.elixir(); 
    const auto yxlo = yxlof.array(); 
    const auto yxhi = yxhif.array(); 
    const auto yzlo = yzlof.array(); 
    const auto yzhi = yzhif.array(); 

    //Z fabs 
    FArrayBox zxlof(g1bx, ncomp); 
    FArrayBox zxhif(g1bx, ncomp); 
    FArrayBox zylof(g1bx, ncomp); 
    FArrayBox zyhif(g1bx, ncomp); 
    Elixir zxle = zxlof.elixir(); 
    Elixir zxhe = zxhif.elixir(); 
    Elixir zyle = zylof.elixir(); 
    Elixir zyhe = zyhif.elixir(); 
    const auto zxlo = zxlof.array(); 
    const auto zxhi = zxhif.array(); 
    const auto zylo = zylof.array(); 
    const auto zyhi = zyhif.array(); 

    auto const xybx = surroundingNodes(grow(bx, 2, 1), 0);
    auto const xzbx = surroundingNodes(grow(bx, 1, 1), 0);  
    auto const yxbx = surroundingNodes(grow(bx, 2, 1), 1); 
    auto const yzbx = surroundingNodes(grow(bx, 0, 1), 1); 
    auto const zxbx = surroundingNodes(grow(bx, 1, 1), 2); 
    auto const zybx = surroundingNodes(grow(bx, 0, 1), 2); 

/*------------------Now perform corner coupling */
    //Dir trans against X
    AMREX_PARALLEL_FOR_4D (yxbx, ncomp, i, j, k, n, {
        const auto bc = BCs[n]; 
        //YX
        Godunov_corner_couple(i,j,k, n, dt, dx, iconserv, ylo, yhi, 
                             s, divu_cc, u_mac, xedge, yxlo, yxhi, 1, 0);
        Godunov_trans_ybc(i, j, k, n, s, yxlo(i,j,k,n), yxhi(i,j,k,n), v_mac,
                                   bc.lo(1), bc.hi(1),
                                   bx.loVect(), bx.hiVect(), true, false);  
    }); 
    
    AMREX_PARALLEL_FOR_4D (zxbx, ncomp, i, j, k, n, {
        const auto bc = BCs[n]; 
        //ZX
        Godunov_corner_couple(i,j,k, n, dt, dx, iconserv, zlo, zhi, 
                             s, divu_cc, u_mac, xedge, zxlo, zxhi, 2, 0);
        Godunov_trans_zbc(i, j, k, n, s, zxlo(i,j,k,n), zxhi(i,j,k,n), w_mac,
                                   bc.lo(2), bc.hi(2),
                                   bx.loVect(), bx.hiVect(), true, false); 
    });

   //Dir trans against Y
    AMREX_PARALLEL_FOR_4D (xybx, ncomp, i, j, k, n, {
        //XY
        const auto bc = BCs[n]; 
        Godunov_corner_couple(i,j,k, n, dt, dy, iconserv, xlo, xhi, 
                             s, divu_cc, v_mac, yedge, xylo, xyhi, 0, 0);
        Godunov_trans_xbc(i, j, k, n, s, xylo(i,j,k,n), xyhi(i,j,k,n), u_mac,
                            bc.lo(0), bc.hi(0), 
                            bx.loVect(), bx.hiVect(), true, false);
    }); 

    AMREX_PARALLEL_FOR_4D (zybx, ncomp, i, j, k, n, {
        const auto bc = BCs[n]; 
        //ZY 
        Godunov_corner_couple(i,j,k, n, dt, dy, iconserv, zlo, zhi, 
                             s, divu_cc, v_mac, yedge, zylo, zyhi, 2, 1);
        Godunov_trans_zbc(i, j, k, n, s, zylo(i,j,k,n), zyhi(i,j,k,n), w_mac,
                                 bc.lo(2), bc.hi(2),
                                 bx.loVect(), bx.hiVect(), false, true);

    });
    //Dir trans against Z
    AMREX_PARALLEL_FOR_4D (xzbx, ncomp, i, j, k, n, {
        //XZ
        const auto bc = BCs[n]; 
        Godunov_corner_couple(i,j,k, n, dt, dz, iconserv, xlo, xhi, 
                             s, divu_cc, w_mac, zedge, xzlo, xzhi, 0, 1);
        Godunov_trans_xbc(i, j, k, n, s, xzlo(i,j,k,n), xzhi(i,j,k,n), u_mac,
                            bc.lo(0), bc.hi(0), 
                            bx.loVect(), bx.hiVect(), false, true); 
    });  

    AMREX_PARALLEL_FOR_4D (yzbx, ncomp, i, j, k, n, {
        const auto bc = BCs[n]; 
        //YZ
        Godunov_corner_couple(i,j,k, n, dt, dz, iconserv, ylo, yhi, 
                             s, divu_cc, w_mac, zedge, yzlo, yzhi, 1, 1);
        Godunov_trans_ybc(i, j, k, n, s, yzlo(i,j,k,n), yzhi(i,j,k,n), v_mac,
                                   bc.lo(1), bc.hi(1),
                                   bx.loVect(), bx.hiVect(), false, true);
    });

   /* Upwinding Edge States */ 
   const auto xbx = surroundingNodes(bx, 0); 
   const auto ybx = surroundingNodes(bx, 1); 
   const auto zbx = surroundingNodes(bx, 2);
 
   AMREX_PARALLEL_FOR_4D (xybx, ncomp,  i, j, k, n, { 
        Real fu = (std::abs(u_mac(i,j,k)) < 1e-06)? 0.0 : 1.0; 
        Real st; 
        st = (u_mac(i,j,k) >= 0)? xylo(i,j,k,n) : xyhi(i,j,k,n); 
        xylo(i,j,k,n) = fu*st 
                      + (1. - fu)*0.5*(xyhi(i,j,k,n) + xylo(i,j,k,n)); //This is safe
    }); 
    
   AMREX_PARALLEL_FOR_4D (xzbx, ncomp,  i, j, k, n, { 
        Real fu = (std::abs(u_mac(i,j,k)) < 1e-06)? 0.0 : 1.0; 
        Real st; 
        st = (u_mac(i,j,k) >= 0)? xzlo(i,j,k,n) : xzhi(i,j,k,n); 
        xzlo(i,j,k,n) = fu*st 
                      + (1. - fu)*0.5*(xzhi(i,j,k,n) + xzlo(i,j,k,n)); 
    });
 
   AMREX_PARALLEL_FOR_4D (yxbx, ncomp, i, j, k, n, { 
        Real fu = (std::abs(v_mac(i,j,k)) < 1e-06)? 0.0 : 1.0; 
        Real st; 
        st = (v_mac(i,j,k) >= 0)? yxlo(i,j,k,n) : yxhi(i,j,k,n); 
        yxlo(i,j,k,n) = fu*st 
                      + (1. - fu)*0.5*(yxhi(i,j,k,n) + yxlo(i,j,k,n));
    }); 

   AMREX_PARALLEL_FOR_4D (yzbx, ncomp, i, j, k, n, { 
        Real fu = (std::abs(v_mac(i,j,k)) < 1e-06)? 0.0 : 1.0; 
        Real st; 
        st = (v_mac(i,j,k) >= 0)? yzlo(i,j,k,n) : yzhi(i,j,k,n); 
        yzlo(i,j,k,n) = fu*st 
                      + (1. - fu)*0.5*(yzhi(i,j,k,n) + yzlo(i,j,k,n)); 
    });
 
   AMREX_PARALLEL_FOR_4D (zxbx, ncomp, i, j, k, n,{ 
        Real fu = (std::abs(w_mac(i,j,k)) < 1e-06)? 0.0 : 1.0;  
        Real st; 
        st = (w_mac(i,j,k) >= 0)? zxlo(i,j,k,n) : zxhi(i,j,k,n); 
        zxlo(i,j,k,n) = fu*st 
                      + (1. - fu)*0.5*(zxhi(i,j,k,n) + zxlo(i,j,k,n));
    }); 

   AMREX_PARALLEL_FOR_4D (zybx, ncomp, i, j, k, n,{ 
        Real fu = (std::abs(w_mac(i,j,k)) < 1e-06)? 0.0 : 1.0;  
        Real st; 
        st = (w_mac(i,j,k) >= 0)? zylo(i,j,k,n) : zyhi(i,j,k,n); 
        zylo(i,j,k,n) = fu*st 
                      + (1. - fu)*0.5*(zyhi(i,j,k,n) + zylo(i,j,k,n));

    }); 

    /* Final Update of Faces */ 

    AMREX_PARALLEL_FOR_4D(xbx, ncomp, i, j, k, n, { 
        Real stl;
        Real sth;
        Real temp; 
        auto bc = BCs[n]; 
//--------------------------------------- X -------------------------------------- 
        if(iconserv[n]==1){
        stl = xlo(i,j,k,n) - (0.5*dt/dy)*(yzlo(i-1,j+1,k,n)*v_mac(i-1,j+1,k)
                           - yzlo(i-1,j,k,n)*v_mac(i-1,j,k))
                           - (0.5*dt/dz)*(zylo(i-1,j,k+1,n)*w_mac(i-1,j,k+1)
                           - zylo(i-1,j,k,n)*w_mac(i-1,j,k))
                           + (0.5*dt/dy)*s(i-1,j,k,n)*(v_mac(i-1,j+1,k) -v_mac(i-1,j,k))
                           + (0.5*dt/dz)*s(i-1,j,k,n)*(w_mac(i-1,j,k+1) -w_mac(i-1,j,k));

        sth = xhi(i,j,k,n) - (0.5*dt/dy)*(yzlo(i,j+1,k,n)*v_mac(i,j+1,k)
                           - yzlo(i,j,k,n)*v_mac(i,j,k))
                           - (0.5*dt/dz)*(zylo(i,j,k+1,n)*w_mac(i,j,k+1)
                           - zylo(i,j,k,n)*w_mac(i,j,k))
                           + (0.5*dt/dy)*s(i,j,k,n)*(v_mac(i,j+1,k) -v_mac(i,j,k))
                           + (0.5*dt/dz)*s(i,j,k,n)*(w_mac(i,j,k+1) -w_mac(i,j,k)); 
        }
        else{
        stl = xlo(i,j,k,n) - (0.25*dt/dy)*(v_mac(i-1,j+1,k)+v_mac(i-1,j,k))*
                             (yzlo(i-1,j+1,k,n) - yzlo(i-1,j,k,n))
                           - (0.25*dt/dz)*(w_mac(i-1,j,k+1)+w_mac(i-1,j,k))*
                             (zylo(i-1,j,k+1,n) - zylo(i-1,j,k,n));

        sth = xhi(i,j,k,n) - (0.25*dt/dy)*(v_mac(i,j+1,k)+v_mac(i,j,k))*
                             (yzlo(i,j+1,k,n) - yzlo(i,j,k,n))
                           - (0.25*dt/dz)*(w_mac(i,j,k+1)+w_mac(i,j,k))*
                             (zylo(i,j,k+1,n) - zylo(i,j,k,n));
        }
        Real stlold = stl; 
        Real sthold = sth; 
       
        Godunov_cc_xbc(i, j, k, n, s, stl, sth, u_mac, bc.lo(0), bc.hi(0),
                                 bx.loVect()[0], bx.hiVect()[0]);  
        if(stl>1e10 || sth > 1e10){
            std::cout << "old = " << stlold << '\t' << sthold << std::endl; 
            std::cout << "New" << std::endl; 
            std::cout<< stl << '\t' << sth << '\t' << i << '\t' << j << '\t' << k << "  X" << std::endl;
            std::cin.get();          }
        temp = (u_mac(i,j,k) >= 0.e0) ? stl : sth; 
        temp = (std::abs(u_mac(i,j,k)) < 1e-06) ? 0.5*(stl + sth) : temp;
        a_fx(i,j,k,n) = temp;
     }); 

    AMREX_PARALLEL_FOR_4D(ybx, ncomp, i, j, k, n, { 
        Real stl;
        Real sth;
        Real temp; 
        auto bc = BCs[n]; 
//-------------------------------------- Y ------------------------------------            
        if(iconserv[n]==1){
        stl = ylo(i,j,k,n) - (0.5*dt/dx)*(xzlo(i+1,j-1,k,n)*u_mac(i+1,j-1,k)
                           - xzlo(i,j-1,k,n)*u_mac(i,j-1,k))
                           - (0.5*dt/dz)*(zxlo(i,j-1,k+1,n)*w_mac(i,j-1,k+1)
                           - zxlo(i,j-1,k,n)*w_mac(i,j-1,k))
                           + (0.5*dt/dx)*s(i,j-1,k,n)*(u_mac(i+1,j-1,k) -u_mac(i,j-1,k))
                           + (0.5*dt/dz)*s(i,j-1,k,n)*(w_mac(i,j-1,k+1) -w_mac(i,j-1,k));

        sth = yhi(i,j,k,n) - (0.5*dt/dx)*(xzlo(i+1,j,k,n)*u_mac(i+1,j,k)
                           - xzlo(i,j,k,n)*u_mac(i,j,k))
                           - (0.5*dt/dz)*(zxlo(i,j,k+1,n)*w_mac(i,j,k+1)
                           - zxlo(i,j,k,n)*w_mac(i,j,k))
                           + (0.5*dt/dx)*s(i,j,k,n)*(u_mac(i+1,j,k) -u_mac(i,j,k))
                           + (0.5*dt/dz)*s(i,j,k,n)*(w_mac(i,j,k+1) -w_mac(i,j,k)); 
        }
        else{
        stl = ylo(i,j,k,n) - (0.25*dt/dx)*(u_mac(i+1,j-1,k)+u_mac(i,j-1,k))*
                             (xzlo(i+1,j-1,k,n) - xzlo(i,j-1,k,n))
                           - (0.25*dt/dz)*(w_mac(i,j-1,k+1)+w_mac(i,j-1,k))*
                             (zxlo(i,j-1,k+1,n) - zxlo(i,j-1,k,n));

        sth = yhi(i,j,k,n) - (0.25*dt/dx)*(u_mac(i+1,j,k)+u_mac(i,j,k))*
                             (xzlo(i+1,j,k,n) - xzlo(i,j,k,n))
                           - (0.25*dt/dz)*(w_mac(i,j,k+1)+w_mac(i,j,k))*
                             (zxlo(i,j,k+1,n) - zxlo(i,j,k,n));
        }
        Godunov_cc_ybc(i, j, k, n, s, stl, sth, v_mac, bc.lo(1), bc.hi(1), 
                                  bx.loVect()[1], bx.hiVect()[1]); 
        if(stl>1e10 || sth > 1e10){
            std::cout<< stl << '\t' << sth << '\t' << i << '\t' << j << '\t' << k << "  Y" << std::endl;
            std::cin.get();  
        }

        temp = (v_mac(i,j,k) >= 0.e0) ? stl : sth; 
        temp = (std::abs(v_mac(i,j,k)) < 1e-06) ? 0.5*(stl + sth) : temp; 
        a_fy(i,j,k,n) = temp;
      });

     AMREX_PARALLEL_FOR_4D(zbx, ncomp, i, j, k, n, { 
        Real stl;
        Real sth;
        Real temp; 
        auto bc = BCs[n]; 
//----------------------------------- Z ----------------------------------------- 
        if (iconserv[n]==1)
        {
            stl = zlo(i,j,k,n) - (0.5*dt/dx)*(xylo(i+1,j,k-1,n)*u_mac(i+1,j,k-1)
                               - xylo(i,j,k-1,n)*u_mac(i,j,k-1))
                               - (0.5*dt/dy)*(yxlo(i,j+1,k-1,n)*v_mac(i,j+1,k-1)
                               - yxlo(i,j,k-1,n)*v_mac(i,j,k-1))
                               + (0.5*dt/dx)*s(i,j,k-1,n)*(u_mac(i+1,j,k-1) -u_mac(i,j,k-1))
                               + (0.5*dt/dy)*s(i,j,k-1,n)*(v_mac(i,j+1,k-1) -v_mac(i,j,k-1));

            sth = zhi(i,j,k,n) - (0.5*dt/dx)*(xylo(i+1,j,k,n)*u_mac(i+1,j,k)
                               - xylo(i,j,k,n)*u_mac(i,j,k))
                               - (0.5*dt/dy)*(yxlo(i,j+1,k,n)*v_mac(i,j+1,k)
                               - yxlo(i,j,k,n)*v_mac(i,j,k))
                               + (0.5*dt/dx)*s(i,j,k,n)*(u_mac(i+1,j,k) -u_mac(i,j,k))
                               + (0.5*dt/dy)*s(i,j,k,n)*(v_mac(i,j+1,k) -v_mac(i,j,k)); 
        } else
        {
            stl = zlo(i,j,k,n) - (0.25*dt/dx)*(u_mac(i+1,j,k-1)+u_mac(i,j,k-1))*
                                 (xylo(i+1,j,k-1,n) - xylo(i,j,k-1,n))
                               - (0.25*dt/dy)*(v_mac(i,j+1,k-1)+v_mac(i,j,k-1))*
                                 (yxlo(i,j+1,k-1,n) - yxlo(i,j,k-1,n));

            sth = zhi(i,j,k,n) - (0.25*dt/dx)*(u_mac(i+1,j,k)+u_mac(i,j,k))*
                                 (xylo(i+1,j,k,n) - xylo(i,j,k,n))
                               - (0.25*dt/dy)*(v_mac(i,j+1,k)+v_mac(i,j,k))*
                                 (yxlo(i,j+1,k,n) - yxlo(i,j,k,n));
        }
        Real stlold = stl; 
        Real sthold = sth; 
        Godunov_cc_zbc(i, j, k, n, s, stl, sth, w_mac, bc.lo(2), bc.hi(2), 
                                        bx.loVect()[2], bx.hiVect()[2]);  
        if(stl>1e10 || sth > 1e10){
            std::cout << "old = " << stlold << '\t' << sthold << std::endl; 
            std::cout << "New" << std::endl; 
            std::cout<< stl << '\t' << sth << '\t' << i << '\t' << j << '\t' << k << "  Z" << std::endl;
            std::cin.get();  
        }

        temp = (w_mac(i,j,k) >= 0.e0) ? stl : sth; 
        temp = (std::abs(w_mac(i,j,k)) < 1e-06) ? 0.5*(stl + sth) : temp; 
        a_fz(i,j,k,n) = temp;
    }); 

    const Box fxbx = surroundingNodes(bx, 0);
    const Box fybx = surroundingNodes(bx, 1);
    const Box fzbx = surroundingNodes(bx, 2);

   AMREX_PARALLEL_FOR_4D(fxbx, ncomp, i, j, k, n, {
        a_fx(i,j,k,n) = a_fx(i,j,k,n)*u_mac(i,j,k);
    });
    AMREX_PARALLEL_FOR_4D(fybx, ncomp, i, j, k, n, {
        a_fy(i,j,k,n) = a_fy(i,j,k,n)*v_mac(i,j,k);
    });
    AMREX_PARALLEL_FOR_4D(fzbx, ncomp, i, j, k, n, {
        a_fz(i,j,k,n) = a_fz(i,j,k,n)*w_mac(i,j,k);
    });
#endif
}