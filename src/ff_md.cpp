#include "ff_md.h"
#include "timer.h"
#include "neighbor_md.h"
#include "atoms_md.h"
#include "xmath.h"
using namespace MAPP_NS;
/*--------------------------------------------
 
 --------------------------------------------*/
ForceFieldMD::ForceFieldMD(AtomsMD* __atoms):
ForceField(__atoms),
atoms(__atoms)
{
    neighbor=new NeighborMD(__atoms,cut_sk_sq);
    f=new Vec<type0>(atoms,__dim__,"f");
}
/*--------------------------------------------
 
 --------------------------------------------*/
ForceFieldMD::~ForceFieldMD()
{
    delete f;
    delete neighbor;
}
/*--------------------------------------------
 
 --------------------------------------------*/
void ForceFieldMD::reset()
{
    type0* __f=f->begin();
    const int n=f->vec_sz*__dim__;
    for(int i=0;i<n;i++) __f[i]=0.0;
    for(int i=0;i<__nvoigt__+1;i++) __vec_lcl[i]=0.0;
}
/*--------------------------------------------
 
 --------------------------------------------*/
void ForceFieldMD::pre_init()
{
    dof_empty=atoms->dof->is_empty();
    type0 tmp;
    max_cut=0.0;
    for(size_t i=0;i<nelems;i++)
        for(size_t j=0;j<i+1;j++)
        {
            tmp=cut[i][j];
            max_cut=MAX(max_cut,tmp);
            cut_sq[i][j]=cut_sq[j][i]=tmp*tmp;
            tmp+=atoms->comm.skin;
            cut_sk_sq[i][j]=cut_sk_sq[j][i]=tmp*tmp;
        }
}
/*--------------------------------------------
 
 --------------------------------------------*/
void ForceFieldMD::post_fin()
{
}
/*--------------------------------------------
 
 --------------------------------------------*/
void ForceFieldMD::pre_xchng_energy_timer(GCMC* gcmc)
{
    pre_xchng_energy(gcmc);
}
/*--------------------------------------------
 
 --------------------------------------------*/
type0 ForceFieldMD::xchng_energy_timer(GCMC* gcmc)
{
    return xchng_energy(gcmc);
}
/*--------------------------------------------
 
 --------------------------------------------*/
void ForceFieldMD::post_xchng_energy_timer(GCMC* gcmc)
{
    post_xchng_energy(gcmc);
}
/*--------------------------------------------
 
 --------------------------------------------*/
void ForceFieldMD::force_calc_timer()
{
    reset();
    force_calc();
    
    if(!dof_empty)
    {
        type0* fvec=f->begin();
        bool* dof=atoms->dof->begin();
        const int natms_lcl=atoms->natms_lcl;
        for(int i=0;i<natms_lcl;i++,fvec+=__dim__,dof+=__dim__)
            Algebra::Do<__dim__>::func([&dof,&fvec](int i){fvec[i]=dof[i] ? fvec[i]:0.0;});
    }
    
    MPI_Allreduce(__vec_lcl,__vec,__nvoigt__+1,Vec<type0>::MPI_T,MPI_SUM,world);
    const type0 vol=atoms->vol;
    Algebra::Do<__nvoigt__>::func([this,&vol](int i){__vec[i+1]/=vol;});
    atoms->pe=__vec[0];
    Algebra::DyadicV_2_MSY(__vec+1,atoms->S_pe);
}
/*--------------------------------------------
 
 --------------------------------------------*/
type0 ForceFieldMD::value_timer()
{
    __vec_lcl[0]=0.0;
    energy_calc();
    type0 en;
    MPI_Allreduce(&__vec_lcl[0],&en,1,Vec<type0>::MPI_T,MPI_SUM,world);
    return en;
}
/*--------------------------------------------
 
 --------------------------------------------*/
void ForceFieldMD::derivative_timer()
{
    reset();
    force_calc();
    MPI_Allreduce(__vec_lcl,__vec,__nvoigt__+1,Vec<type0>::MPI_T,MPI_SUM,world);
    Algebra::Do<__nvoigt__>::func([this](int i){__vec[i+1]/=atoms->vol;});
    atoms->pe=__vec[0];
    Algebra::DyadicV_2_MSY(__vec+1,atoms->S_pe);
    
    if(!dof_empty)
    {
        type0* fvec=f->begin();
        bool* dof=atoms->dof->begin();
        const int n=atoms->natms_lcl*__dim__;
        for(int i=0;i<n;i++) fvec[i]=dof[i] ? fvec[i]:0.0;
    }
}
/*--------------------------------------------
 this does not sound right hs to be check later
 --------------------------------------------*/
void ForceFieldMD::derivative_timer(type0(*&S)[__dim__])
{
    reset();
    force_calc();
    type0* fvec=f->begin();
    type0* xvec=atoms->x->begin();
    if(dof_empty)
    {
        const int natms_lcl=atoms->natms_lcl;
        for(int i=0;i<natms_lcl;i++,fvec+=__dim__,xvec+=__dim__)
            Algebra::DyadicV<__dim__>(xvec,fvec,__vec_lcl+1);
    }
    else
    {
        bool* dof=atoms->dof->begin();
        const int natms_lcl=atoms->natms_lcl;
        for(int i=0;i<natms_lcl;i++,fvec+=__dim__,xvec+=__dim__,dof+=__dim__)
        {
            Algebra::Do<__dim__>::func([&dof,&fvec](int i){fvec[i]=dof[i] ? fvec[i]:0.0;});
            Algebra::DyadicV<__dim__>(xvec,fvec,__vec_lcl+1);
        }
    }
    
    
    MPI_Allreduce(__vec_lcl,__vec,__nvoigt__+1,Vec<type0>::MPI_T,MPI_SUM,world);
    Algebra::Do<__nvoigt__>::func([this](int i){__vec[i+1]*=-1.0;});
    Algebra::NONAME_DyadicV_mul_MLT(__vec+1,atoms->B,S);
    const type0 vol=atoms->vol;
    Algebra::Do<__nvoigt__>::func([this,&vol](int i){__vec[i+1]/=-vol;});
    atoms->pe=__vec[0];
    Algebra::DyadicV_2_MSY(__vec+1,atoms->S_pe);
}
/*--------------------------------------------
 this does not sound right hs to be check later
 --------------------------------------------*/
void ForceFieldMD::derivative_timer(bool affine,type0(*&S)[__dim__])
{
    reset();
    force_calc();
    if(!affine)
    {
        type0* fvec=f->begin();
        type0* xvec=atoms->x->begin();
        if(dof_empty)
        {
            const int natms_lcl=atoms->natms_lcl;
            for(int i=0;i<natms_lcl;i++,fvec+=__dim__,xvec+=__dim__)
                Algebra::DyadicV<__dim__>(xvec,fvec,__vec_lcl+1);
        }
        else
        {
            bool* dof=atoms->dof->begin();
            const int natms_lcl=atoms->natms_lcl;
            for(int i=0;i<natms_lcl;i++,fvec+=__dim__,xvec+=__dim__,dof+=__dim__)
            {
                Algebra::Do<__dim__>::func([&dof,&fvec](int i){fvec[i]=dof[i] ? fvec[i]:0.0;});
                Algebra::DyadicV<__dim__>(xvec,fvec,__vec_lcl+1);
            }
        }
    }
    
    MPI_Allreduce(__vec_lcl,__vec,__nvoigt__+1,Vec<type0>::MPI_T,MPI_SUM,world);
    Algebra::Do<__nvoigt__>::func([this](int i){__vec[i+1]*=-1.0;});
    Algebra::NONAME_DyadicV_mul_MLT(__vec+1,atoms->B,S);
    const type0 vol=atoms->vol;
    Algebra::Do<__nvoigt__>::func([this,&vol](int i){__vec[i+1]/=-vol;});
    atoms->pe=__vec[0];
    Algebra::DyadicV_2_MSY(__vec+1,atoms->S_pe);

}









