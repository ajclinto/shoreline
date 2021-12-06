out vec4 frag_color;

uniform sampler2DRect s_texture;

uniform vec2 wsize;
uniform vec2 off;
uniform float zoom;

layout(origin_upper_left) in vec4 gl_FragCoord;

void main(void)
{
    vec2 tsize = textureSize(s_texture);
    vec2 coord = (gl_FragCoord.xy - wsize + off) / zoom;
    coord += tsize/2;
    if (coord.x >= 0.0 && coord.x <= tsize.x &&
        coord.y >= 0.0 && coord.y <= tsize.y)
    {
        frag_color = texture(s_texture, coord);
    }
    else if (coord.x >= -1.0/zoom && coord.x <= tsize.x+1.0/zoom &&
             coord.y >= -1.0/zoom && coord.y <= tsize.y+1.0/zoom)
    {
        // Draw a 1 pixel border
        frag_color = vec4(0.5, 0.5, 0.5, 1);
    }
    else
    {
        frag_color = vec4(0, 0, 0, 1);
    }
}
