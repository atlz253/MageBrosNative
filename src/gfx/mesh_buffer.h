// A vertex buffer, index buffer and vertex layout for geometry that does not change (models and
// the merged level meshes). The data is uploaded on the first draw; Invalidate() makes the next
// draw upload it again. Device::DrawMesh keeps the game's draw order and the shared state cache.
#pragma once
#include "../core/d3dtypes.h"
#include "gl.h"

class MeshBuffer {
public:
    MeshBuffer() = default;
    ~MeshBuffer();
    MeshBuffer(const MeshBuffer&) = delete;
    MeshBuffer& operator=(const MeshBuffer&) = delete;

    // Uploads when the mesh is new, invalidated, or its format/size changed. Leaves its layout
    // bound while it sets the attributes.
    void Ensure(DWORD fvf, const void* vertices, DWORD numVerts, const WORD* indices, DWORD numIndices);
    // The layout must be bound by the caller (Device::DrawMesh does so).
    void DrawIndexed(DWORD firstIndex, DWORD indexCount) const;
    void Invalidate() { valid_ = false; }
    GLuint Vao() const { return vao_; }

private:
    GLuint vao_ = 0, vbo_ = 0, ibo_ = 0;
    DWORD fvf_ = 0, verts_ = 0, indices_ = 0;
    bool valid_ = false;
};
