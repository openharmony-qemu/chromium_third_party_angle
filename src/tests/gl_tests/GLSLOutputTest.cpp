//
// Copyright 2025 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#ifdef UNSAFE_BUFFERS_BUILD
#    pragma allow_unsafe_buffers
#endif

#include "test_utils/CompilerTest.h"

#include "test_utils/angle_test_configs.h"
#include "test_utils/gl_raii.h"

using namespace angle;

namespace
{
class GLSLOutputTest : public CompilerTest
{
  protected:
    GLSLOutputTest() {}

    // Helper to create a shader
    void compileShader(GLenum shaderType, const char *shaderSource)
    {
        const CompiledShader &shader = compile(shaderType, shaderSource);
        EXPECT_TRUE(shader.success());
    }

    // Check that the output contains (or not) the expected substring.
    void verifyIsInTranslation(GLenum shaderType, const char *expect)
    {
        EXPECT_TRUE(getCompiledShader(shaderType).verifyInTranslatedSource(expect)) << expect;
    }
    void verifyIsNotInTranslation(GLenum shaderType, const char *expect)
    {
        EXPECT_TRUE(getCompiledShader(shaderType).verifyNotInTranslatedSource(expect)) << expect;
    }
};

class GLSLOutputGLSLTest : public GLSLOutputTest
{};

// Verifies that without explicitly enabling GL_EXT_draw_buffers extension in the shader, no
// broadcast emulation.
TEST_P(GLSLOutputGLSLTest, FragColorNoBroadcast)
{
    constexpr char kFS[] = R"(void main()
{
    gl_FragColor = vec4(1, 0, 0, 0);
})";
    compileShader(GL_FRAGMENT_SHADER, kFS);
    verifyIsInTranslation(GL_FRAGMENT_SHADER, "gl_FragColor");
    verifyIsNotInTranslation(GL_FRAGMENT_SHADER, "gl_FragData[0]");
    verifyIsNotInTranslation(GL_FRAGMENT_SHADER, "gl_FragData[1]");
}

// Verifies that with explicitly enabling GL_EXT_draw_buffers extension
// in the shader, broadcast is emualted by replacing gl_FragColor with gl_FragData.
TEST_P(GLSLOutputGLSLTest, FragColorBroadcast)
{
    ANGLE_SKIP_TEST_IF(!IsGLExtensionEnabled("GL_EXT_draw_buffers"));

    constexpr char kFS[] = R"(#extension GL_EXT_draw_buffers : require
void main()
{
    gl_FragColor = vec4(1, 0, 0, 0);
})";
    compileShader(GL_FRAGMENT_SHADER, kFS);
    verifyIsNotInTranslation(GL_FRAGMENT_SHADER, "gl_FragColor");
    verifyIsInTranslation(GL_FRAGMENT_SHADER, "gl_FragData[0]");
    verifyIsInTranslation(GL_FRAGMENT_SHADER, "gl_FragData[1]");
}

// Verifies that with explicitly enabling GL_EXT_draw_buffers extension
// in the shader with an empty main(), nothing happens.
TEST_P(GLSLOutputGLSLTest, EmptyMain)
{
    ANGLE_SKIP_TEST_IF(!IsGLExtensionEnabled("GL_EXT_draw_buffers"));

    constexpr char kFS[] = R"(#extension GL_EXT_draw_buffers : require
void main()
{
})";
    compileShader(GL_FRAGMENT_SHADER, kFS);
    verifyIsNotInTranslation(GL_FRAGMENT_SHADER, "gl_FragColor");
    verifyIsNotInTranslation(GL_FRAGMENT_SHADER, "gl_FragData[0]");
    verifyIsNotInTranslation(GL_FRAGMENT_SHADER, "gl_FragData[1]");
}

class GLSLOutputGLSLVerifyIRUseTest : public GLSLOutputGLSLTest
{};

// A basic test that makes sure the `useIr` feature is actually effective.
TEST_P(GLSLOutputGLSLVerifyIRUseTest, Basic)
{
    constexpr char kFS[] = R"(void main()
{
})";
    compileShader(GL_FRAGMENT_SHADER, kFS);
    // With AST, implicit `return` remains implicit.  With IR, every block ends in a branch, so the
    // `return` is explicit.
    if (getEGLWindow()->isFeatureEnabled(Feature::UseIr))
    {
        verifyIsInTranslation(GL_FRAGMENT_SHADER, "return");
    }
    else
    {
        verifyIsNotInTranslation(GL_FRAGMENT_SHADER, "return");
    }
}

// Test that a constant switch with matching case is folded away in the translated shader.
TEST_P(GLSLOutputGLSLTest_ES3, BasicConstantSwitch)
{
    constexpr char kFS[] = R"(#version 300 es
precision highp float;
out vec4 color;
void main()
{
    float f = 0.0;
    switch (0)
    {
        case 156: f = 1234.0; break;
        case 0:   f = 2.0; break;
        default:  f = 3.0; break;
    }
    color = f == 2.0 ? vec4(0, 1, 0, 1) : vec4(1, 0, 0, 1);
})";
    compileShader(GL_FRAGMENT_SHADER, kFS);
    verifyIsNotInTranslation(GL_FRAGMENT_SHADER, "switch");
}

// Test that a constant switch without matching case is completely pruned in the translated shader.
TEST_P(GLSLOutputGLSLTest_ES3, BasicConstantSwitchPruned)
{
    constexpr char kFS[] = R"(#version 300 es
precision highp float;
out vec4 color;
void main()
{
    float f = 0.0;
    switch (10)
    {
        case 156: f = 1234.0; break;
        case 0:   f = 567.0; break;
    }
    color = f == 0.0 ? vec4(0, 1, 0, 1) : vec4(1, 0, 0, 1);
})";
    compileShader(GL_FRAGMENT_SHADER, kFS);
    verifyIsNotInTranslation(GL_FRAGMENT_SHADER, "switch");
}

// Test that nested constant switch statements (https://crbug.com/548066294) are folded away.
TEST_P(GLSLOutputGLSLTest_ES3, NestedConstantSwitch)
{
    constexpr char kFS[] = R"(#version 300 es
precision highp float;
out vec4 color;
void main()
{
    int result = 0;
    switch (0)
    {
        case 0:
            result += 1;
            switch (1)
            {
                case 0:
                    result += 10;
                    break;
                case 1:
                    result += 20;
                    switch (2)
                    {
                        case 2:
                            result += 300;
                            break;
                        default:
                            result += 400;
                            break;
                    }
                    break;
                default:
                    result += 50;
                    break;
            }
            break;
        case 1:
            result += 1000;
            break;
        default:
            result += 2000;
            break;
    }
    color = result == 321 ? vec4(0, 1, 0, 1) : vec4(1, 0, 0, 1);
})";
    compileShader(GL_FRAGMENT_SHADER, kFS);
    verifyIsNotInTranslation(GL_FRAGMENT_SHADER, "switch");
}

// Test that nested constant switch statements (https://crbug.com/548066294) are folded away.
// The selector is length() in this test.
TEST_P(GLSLOutputGLSLTest_ES3, NestedConstantSwitchWithLength)
{
    constexpr char kFS[] = R"(#version 300 es
precision highp float;
out vec4 color;
int sideEffects = 0;
int[1] one()
{
    sideEffects += 1;
    return int[1](1);
}
int[2] two()
{
    sideEffects += 1;
    return int[2](2, 2);
}
void main()
{
    int result = 0;
    switch (one().length() - 1)
    {
        case 0:
            result += 1;
            switch (one().length())
            {
                case 0:
                    result += 10;
                    break;
                case 1:
                    result += 20;
                    switch (two().length())
                    {
                        case 2:
                            result += 300;
                            break;
                        default:
                            result += 400;
                            break;
                    }
                    break;
                default:
                    result += 50;
                    break;
            }
            break;
        case 1:
            result += 1000;
            break;
        default:
            result += 2000;
            break;
    }
    color = result == 321 && sideEffects == 3 ? vec4(0, 1, 0, 1) : vec4(1, 0, 0, 1);
})";
    compileShader(GL_FRAGMENT_SHADER, kFS);
    verifyIsNotInTranslation(GL_FRAGMENT_SHADER, "switch");
}
// Test that fall-through across cases in a constant switch is folded away.
TEST_P(GLSLOutputGLSLTest_ES3, FallThrough)
{
    constexpr char kFS[] = R"(#version 300 es
precision highp float;
out vec4 color;
void main()
{
    float f = 0.0;
    switch (1)
    {
        case 1: f += 5.0;
        case 2: f += 2.0; break;
        case 3: f += 4.0; break;
    }
    color = f == 7.0 ? vec4(0, 1, 0, 1) : vec4(1, 0, 0, 1);
})";
    compileShader(GL_FRAGMENT_SHADER, kFS);
    verifyIsNotInTranslation(GL_FRAGMENT_SHADER, "switch");
}

// Test that conditional break inside folded switch statements emits a do-while loop wrapper.
TEST_P(GLSLOutputGLSLTest_ES3, ConditionalBreak)
{
    constexpr char kFS[] = R"(#version 300 es
precision highp float;
uniform int u_zero;
out vec4 color;
void main()
{
    float f = 0.0;
    switch (0)
    {
        case 0:
            if (u_zero != 0) { break; }
            f = 2.0;
            break;
        default:
            f = 5.0;
            break;
    }
    color = f == 2.0 ? vec4(0, 1, 0, 1) : vec4(1, 0, 0, 1);
})";
    compileShader(GL_FRAGMENT_SHADER, kFS);
    verifyIsNotInTranslation(GL_FRAGMENT_SHADER, "switch");
}

// Test that continue targeting an enclosing loop is preserved when folding switch statements.
TEST_P(GLSLOutputGLSLTest_ES3, ContinueInEnclosingLoop)
{
    constexpr char kFS[] = R"(#version 300 es
precision highp float;
uniform int u_zero;
out vec4 color;
void main()
{
    int count = 0;
    for (int i = 0; i < 4; ++i)
    {
        switch (0)
        {
            case 0:
                if (u_zero != 0) { break; }
                if (i == 2) { continue; }
                count += 1;
                break;
            default:
                count += 100;
                break;
        }
        count += 10;
    }
    color = count == 33 ? vec4(0, 1, 0, 1) : vec4(1, 0, 0, 1);
})";
    compileShader(GL_FRAGMENT_SHADER, kFS);
    verifyIsNotInTranslation(GL_FRAGMENT_SHADER, "switch");
}

// Test that declarations in skipped cases are preserved without initializers in translation.
TEST_P(GLSLOutputGLSLTest_ES3, SkippedDeclaration)
{
    constexpr char kFS[] = R"(#version 300 es
precision highp float;
uniform int u;
out vec4 color;
void main()
{
    float res = 0.0;
    switch (u, 1)
    {
        case 0:
            float d = 4.0;
            break;
        case 2:
            float e = 5.0;
            break;
        case 1:
            d = 2.0;
            res = d;
            break;
    }
    color = res == 2.0 ? vec4(0, 1, 0, 1) : vec4(1, 0, 0, 1);
})";
    compileShader(GL_FRAGMENT_SHADER, kFS);
    verifyIsNotInTranslation(GL_FRAGMENT_SHADER, "switch");
}

// Test that case blocks with compound statements are truncated at break and subsequent cases
// are not emitted in translated output.
TEST_P(GLSLOutputGLSLTest_ES3, CaseWithCompoundStatementBlock)
{
    constexpr char kFS[] = R"(#version 300 es
precision highp float;
uniform int u;
out vec4 color;
void main()
{
    float f = 0.0;
    switch (true ? 0 : u)
    {
        case 0:
        {
            f = 2.0;
            break;
        }
        case 1:
        {
            f = 5.0;
            break;
        }
    }
    color = f == 2.0 ? vec4(0, 1, 0, 1) : vec4(1, 0, 0, 1);
})";
    compileShader(GL_FRAGMENT_SHADER, kFS);
    verifyIsNotInTranslation(GL_FRAGMENT_SHADER, "switch");
}
}  // namespace

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(GLSLOutputGLSLTest);
ANGLE_INSTANTIATE_TEST(GLSLOutputGLSLTest, ES2_OPENGL(), ES2_OPENGLES());

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(GLSLOutputGLSLVerifyIRUseTest);
ANGLE_INSTANTIATE_TEST(GLSLOutputGLSLVerifyIRUseTest,
                       ES2_OPENGL(),
                       ES2_OPENGLES(),
                       ES2_OPENGL().disable(Feature::UseIr),
                       ES2_OPENGLES().disable(Feature::UseIr));
