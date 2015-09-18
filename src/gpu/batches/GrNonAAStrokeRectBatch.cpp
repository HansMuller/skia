/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "GrNonAAStrokeRectBatch.h"

#include "GrBatchTest.h"
#include "GrBatchFlushState.h"
#include "GrColor.h"
#include "GrDefaultGeoProcFactory.h"
#include "GrVertexBatch.h"
#include "SkRandom.h"

/*  create a triangle strip that strokes the specified rect. There are 8
    unique vertices, but we repeat the last 2 to close up. Alternatively we
    could use an indices array, and then only send 8 verts, but not sure that
    would be faster.
    */
static void init_stroke_rect_strip(SkPoint verts[10], const SkRect& rect, SkScalar width) {
    const SkScalar rad = SkScalarHalf(width);
    // TODO we should be able to enable this assert, but we'd have to filter these draws
    // this is a bug
    //SkASSERT(rad < rect.width() / 2 && rad < rect.height() / 2);

    verts[0].set(rect.fLeft + rad, rect.fTop + rad);
    verts[1].set(rect.fLeft - rad, rect.fTop - rad);
    verts[2].set(rect.fRight - rad, rect.fTop + rad);
    verts[3].set(rect.fRight + rad, rect.fTop - rad);
    verts[4].set(rect.fRight - rad, rect.fBottom - rad);
    verts[5].set(rect.fRight + rad, rect.fBottom + rad);
    verts[6].set(rect.fLeft + rad, rect.fBottom - rad);
    verts[7].set(rect.fLeft - rad, rect.fBottom + rad);
    verts[8] = verts[0];
    verts[9] = verts[1];
}

class NonAAStrokeRectBatch : public GrVertexBatch {
public:
    DEFINE_BATCH_CLASS_ID

    struct Geometry {
        SkMatrix fViewMatrix;
        SkRect fRect;
        SkScalar fStrokeWidth;
        GrColor fColor;
    };

    static GrDrawBatch* Create(GrColor color, const SkMatrix& viewMatrix, const SkRect& rect,
                               SkScalar strokeWidth, bool snapToPixelCenters) {
        return new NonAAStrokeRectBatch(color, viewMatrix, rect, strokeWidth, snapToPixelCenters);
    }

    const char* name() const override { return "GrStrokeRectBatch"; }

    void getInvariantOutputColor(GrInitInvariantOutput* out) const override {
        // When this is called on a batch, there is only one geometry bundle
        out->setKnownFourComponents(fGeoData[0].fColor);
    }

    void getInvariantOutputCoverage(GrInitInvariantOutput* out) const override {
        out->setKnownSingleComponent(0xff);
    }

private:
    void onPrepareDraws(Target* target) override {
        SkAutoTUnref<const GrGeometryProcessor> gp;
        {
            using namespace GrDefaultGeoProcFactory;
            Color color(this->color());
            Coverage coverage(this->coverageIgnored() ? Coverage::kSolid_Type :
                                                        Coverage::kNone_Type);
            LocalCoords localCoords(this->usesLocalCoords() ? LocalCoords::kUsePosition_Type :
                                                              LocalCoords::kUnused_Type);
            gp.reset(GrDefaultGeoProcFactory::Create(color, coverage, localCoords,
                                                     this->viewMatrix()));
        }

        target->initDraw(gp, this->pipeline());

        size_t vertexStride = gp->getVertexStride();

        SkASSERT(vertexStride == sizeof(GrDefaultGeoProcFactory::PositionAttr));

        Geometry& args = fGeoData[0];

        int vertexCount = kVertsPerHairlineRect;
        if (args.fStrokeWidth > 0) {
            vertexCount = kVertsPerStrokeRect;
        }

        const GrVertexBuffer* vertexBuffer;
        int firstVertex;

        void* verts = target->makeVertexSpace(vertexStride, vertexCount, &vertexBuffer,
                                              &firstVertex);

        if (!verts) {
            SkDebugf("Could not allocate vertices\n");
            return;
        }

        SkPoint* vertex = reinterpret_cast<SkPoint*>(verts);

        GrPrimitiveType primType;

        if (args.fStrokeWidth > 0) {;
            primType = kTriangleStrip_GrPrimitiveType;
            args.fRect.sort();
            init_stroke_rect_strip(vertex, args.fRect, args.fStrokeWidth);
        } else {
            // hairline
            primType = kLineStrip_GrPrimitiveType;
            vertex[0].set(args.fRect.fLeft, args.fRect.fTop);
            vertex[1].set(args.fRect.fRight, args.fRect.fTop);
            vertex[2].set(args.fRect.fRight, args.fRect.fBottom);
            vertex[3].set(args.fRect.fLeft, args.fRect.fBottom);
            vertex[4].set(args.fRect.fLeft, args.fRect.fTop);
        }

        GrVertices vertices;
        vertices.init(primType, vertexBuffer, firstVertex, vertexCount);
        target->draw(vertices);
    }

    void initBatchTracker(const GrPipelineOptimizations& opt) override {
        // Handle any color overrides
        if (!opt.readsColor()) {
            fGeoData[0].fColor = GrColor_ILLEGAL;
        }
        opt.getOverrideColorIfSet(&fGeoData[0].fColor);

        // setup batch properties
        fBatch.fColorIgnored = !opt.readsColor();
        fBatch.fColor = fGeoData[0].fColor;
        fBatch.fUsesLocalCoords = opt.readsLocalCoords();
        fBatch.fCoverageIgnored = !opt.readsCoverage();
    }

    NonAAStrokeRectBatch(GrColor color, const SkMatrix& viewMatrix, const SkRect& rect,
                         SkScalar strokeWidth, bool snapToPixelCenters)
        : INHERITED(ClassID()) {
        Geometry& geometry = fGeoData.push_back();
        geometry.fViewMatrix = viewMatrix;
        geometry.fRect = rect;
        geometry.fStrokeWidth = strokeWidth;
        geometry.fColor = color;

        fBatch.fHairline = geometry.fStrokeWidth == 0;

        fGeoData.push_back(geometry);

        // setup bounds
        fBounds = geometry.fRect;
        SkScalar rad = SkScalarHalf(geometry.fStrokeWidth);
        fBounds.outset(rad, rad);
        geometry.fViewMatrix.mapRect(&fBounds);

        // If our caller snaps to pixel centers then we have to round out the bounds
        if (snapToPixelCenters) {
            fBounds.roundOut();
        }
    }

    GrColor color() const { return fBatch.fColor; }
    bool usesLocalCoords() const { return fBatch.fUsesLocalCoords; }
    bool colorIgnored() const { return fBatch.fColorIgnored; }
    const SkMatrix& viewMatrix() const { return fGeoData[0].fViewMatrix; }
    bool hairline() const { return fBatch.fHairline; }
    bool coverageIgnored() const { return fBatch.fCoverageIgnored; }

    bool onCombineIfPossible(GrBatch* t, const GrCaps&) override {
        // if (!GrPipeline::CanCombine(*this->pipeline(), this->bounds(), *t->pipeline(),
        //     t->bounds(), caps)) {
        //     return false;
        // }
        // GrStrokeRectBatch* that = t->cast<StrokeRectBatch>();

        // NonAA stroke rects cannot batch right now
        // TODO make these batchable
        return false;
    }

    struct BatchTracker {
        GrColor fColor;
        bool fUsesLocalCoords;
        bool fColorIgnored;
        bool fCoverageIgnored;
        bool fHairline;
    };

    const static int kVertsPerHairlineRect = 5;
    const static int kVertsPerStrokeRect = 10;

    BatchTracker fBatch;
    SkSTArray<1, Geometry, true> fGeoData;

    typedef GrVertexBatch INHERITED;
};

namespace GrNonAAStrokeRectBatch {

GrDrawBatch* Create(GrColor color,
                    const SkMatrix& viewMatrix,
                    const SkRect& rect,
                    SkScalar strokeWidth,
                    bool snapToPixelCenters) {
    return NonAAStrokeRectBatch::Create(color, viewMatrix, rect, strokeWidth, snapToPixelCenters);
}

};

#ifdef GR_TEST_UTILS

DRAW_BATCH_TEST_DEFINE(NonAAStrokeRectBatch) {
    SkMatrix viewMatrix = GrTest::TestMatrix(random);
    GrColor color = GrRandomColor(random);
    SkRect rect = GrTest::TestRect(random);
    SkScalar strokeWidth = random->nextBool() ? 0.0f : 1.0f;

    return NonAAStrokeRectBatch::Create(color, viewMatrix, rect, strokeWidth, random->nextBool());
}

#endif