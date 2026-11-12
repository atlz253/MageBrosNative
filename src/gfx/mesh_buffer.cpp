#include "mesh_buffer.h"
#include "device.h"

#define MBN_GL(expr) (GfxFrameWork::CountGLCall(), (expr))

MeshBuffer::~MeshBuffer() {
    if (vbo_) MBN_GL(glDeleteBuffers(1, &vbo_));
    if (ibo_) MBN_GL(glDeleteBuffers(1, &ibo_));
    if (vao_) MBN_GL(glDeleteVertexArrays(1, &vao_));
}

void MeshBuffer::Ensure(DWORD fvf, const void* vertices, DWORD numVerts, const WORD* indices, DWORD numIndices) {
    if (!vao_) {
        MBN_GL(glGenVertexArrays(1, &vao_));
        MBN_GL(glGenBuffers(1, &vbo_));
        MBN_GL(glGenBuffers(1, &ibo_));
    }
    if (valid_ && fvf_ == fvf && verts_ == numVerts && indices_ == numIndices) return;

    const size_t stride = fvf == D3DFVF_VERTEX ? sizeof(D3DVERTEX) : sizeof(D3DLVERTEX);
    MBN_GL(glBindVertexArray(vao_));
    MBN_GL(glBindBuffer(GL_ARRAY_BUFFER, vbo_));
    MBN_GL(glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(stride * numVerts), vertices, GL_STATIC_DRAW));
    GfxFrameWork::CountUploadedBytes((uint64_t)stride * numVerts);
    MBN_GL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_));
    MBN_GL(glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(sizeof(WORD) * numIndices), indices, GL_STATIC_DRAW));
    GfxFrameWork::CountUploadedBytes((uint64_t)sizeof(WORD) * numIndices);

    for (int i = 0; i < 6; i++) MBN_GL(glDisableVertexAttribArray(i));
    if (fvf == D3DFVF_VERTEX) {
        MBN_GL(glEnableVertexAttribArray(0));
        MBN_GL(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0));
        MBN_GL(glEnableVertexAttribArray(1));
        MBN_GL(glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)12));
        MBN_GL(glEnableVertexAttribArray(4));
        MBN_GL(glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, stride, (void*)24));
        MBN_GL(glVertexAttrib4f(2, 1, 1, 1, 1));
    } else {
        MBN_GL(glEnableVertexAttribArray(0));
        MBN_GL(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0));
        MBN_GL(glEnableVertexAttribArray(2));
#if defined(MBN_GLES)
        // ES has no BGRA vertex-array form: four normalised bytes, swizzled in the shader (uBgra)
        MBN_GL(glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)16));
#else
        MBN_GL(glVertexAttribPointer(2, GL_BGRA, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)16));
#endif
        MBN_GL(glEnableVertexAttribArray(4));
        MBN_GL(glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, stride, (void*)24));
        MBN_GL(glVertexAttrib4f(1, 0, 0, 0, 0));
    }
    fvf_ = fvf;
    verts_ = numVerts;
    indices_ = numIndices;
    valid_ = true;
}

void MeshBuffer::DrawIndexed(DWORD firstIndex, DWORD indexCount) const {
    MBN_GL(glDrawElements(GL_TRIANGLES, (GLsizei)indexCount, GL_UNSIGNED_SHORT,
                          (const void*)((size_t)firstIndex * sizeof(WORD))));
}
