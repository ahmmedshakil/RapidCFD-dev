/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright (C) 2011-2014 OpenFOAM Foundation
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

\*---------------------------------------------------------------------------*/

#include "fvPatch.H"
#include "addToRunTimeSelectionTable.H"
#include "fvBoundaryMesh.H"
#include "fvMesh.H"
#include "primitiveMesh.H"
#include "volFields.H"
#include "surfaceFields.H"

// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

namespace Foam
{
    defineTypeNameAndDebug(fvPatch, 0);
    defineRunTimeSelectionTable(fvPatch, polyPatch);
    addToRunTimeSelectionTable(fvPatch, fvPatch, polyPatch);
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

Foam::fvPatch::fvPatch(const polyPatch& p, const fvBoundaryMesh& bm)
:
    polyPatch_(p),
    boundaryMesh_(bm)
{}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

Foam::fvPatch::~fvPatch()
{}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

bool Foam::fvPatch::constraintType(const word& pt)
{
    return fvPatchField<scalar>::patchConstructorTablePtr_->found(pt);
}


Foam::wordList Foam::fvPatch::constraintTypes()
{
    wordList cTypes(polyPatchConstructorTablePtr_->size());

    label i = 0;

    for
    (
        polyPatchConstructorTable::iterator cstrIter =
            polyPatchConstructorTablePtr_->begin();
        cstrIter != polyPatchConstructorTablePtr_->end();
        ++cstrIter
    )
    {
        if (constraintType(cstrIter.key()))
        {
            cTypes[i++] = cstrIter.key();
        }
    }

    cTypes.setSize(i);

    return cTypes;
}


const Foam::labelgpuList& Foam::fvPatch::faceCells() const
{
    return polyPatch_.getFaceCells();
}

const Foam::labelList& Foam::fvPatch::faceCellsHost() const
{
    return polyPatch_.faceCells();
}

const Foam::vectorgpuField& Foam::fvPatch::Cf() const
{
    return boundaryMesh().mesh().Cf().boundaryField()[index()];
}


Foam::tmp<Foam::vectorgpuField> Foam::fvPatch::Cn() const
{
    tmp<vectorgpuField> tcc(new vectorgpuField(size()));
    vectorgpuField& cc = tcc();

    const labelgpuList& faceCells = this->faceCells();

    // get reference to global cell centres
    const vectorgpuField& gcc = boundaryMesh().mesh().getCellCentres();

    auto gccIter = thrust::make_permutation_iterator(
        gcc.begin(),
        faceCells.begin()
    );
    thrust::copy(gccIter, gccIter + faceCells.size(), cc.begin());

    return tcc;
}


Foam::tmp<Foam::vectorgpuField> Foam::fvPatch::nf() const
{
    return Sf()/magSf();
}


const Foam::vectorgpuField& Foam::fvPatch::Sf() const
{
    return boundaryMesh().mesh().Sf().boundaryField()[index()];
}


const Foam::scalargpuField& Foam::fvPatch::magSf() const
{
    return boundaryMesh().mesh().magSf().boundaryField()[index()];
}

namespace Foam {
struct fvPatchDelta {
    template<class Tuple>
    __host__ __device__
    vector operator()(const Tuple& t)
    {
        const vector& S = thrust::get<0>(t);
        const scalar& magS = thrust::get<1>(t);
        const vector& C = thrust::get<2>(t);
        const vector& Cn = thrust::get<3>(t);

        const vector nHat = S/magS;

        return nHat*(nHat & (C - Cn));
    }
};
}

Foam::tmp<Foam::vectorgpuField> Foam::fvPatch::delta() const
{
    tmp<vectorgpuField> tout(new vectorgpuField(size()));
    vectorgpuField& out = tout();

    const vectorgpuField& S = Sf();
    const scalargpuField& magS = magSf();
    const vectorgpuField& C = Cf();

    const vectorgpuField& gcc = boundaryMesh().mesh().getCellCentres();
    const labelgpuList& faceCells = this->faceCells();

    // Use patch-normal delta for all non-coupled BCs
    //const vectorgpuField nHat(nf());
    //return nHat*(nHat & (Cf() - Cn()));

    auto iter = thrust::make_zip_iterator(thrust::make_tuple(
        S.begin(),
        magS.begin(),
        C.begin(),
        thrust::make_permutation_iterator(
            gcc.begin(),
            faceCells.begin()
        )
    ));
    thrust::transform(iter, iter+size(), out.begin(), fvPatchDelta());
    return tout;
}


void Foam::fvPatch::makeWeights(scalargpuField& w) const
{
    w = 1.0;
}


void Foam::fvPatch::initMovePoints()
{}


void Foam::fvPatch::movePoints()
{}


const Foam::scalargpuField& Foam::fvPatch::deltaCoeffs() const
{
    return boundaryMesh().mesh().deltaCoeffs().boundaryField()[index()];
}


const Foam::scalargpuField& Foam::fvPatch::weights() const
{
    return boundaryMesh().mesh().weights().boundaryField()[index()];
}


// ************************************************************************* //
