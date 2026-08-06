#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;

uniform vec3 u_color;
uniform bool u_is_horizon;
uniform vec3 u_light_pos;
uniform vec3 u_view_pos;

void main() {
    if (u_is_horizon) {
        // Render a dark, matte black sphere representing the event horizon.
        // We add a subtle dark-grey diffuse lighting and a soft rim light
        // so the user can perceive its 3D volume at the bottom of the funnel.
        vec3 normal = normalize(Normal);
        vec3 light_dir = normalize(u_light_pos - FragPos);
        vec3 view_dir = normalize(u_view_pos - FragPos);
        
        // Faint ambient + diffuse
        float ambient = 0.03;
        float diff = max(0.0, dot(normal, light_dir)) * 0.12;
        
        // Rim lighting (glowing edges)
        float rim = 1.0 - max(0.0, dot(normal, view_dir));
        rim = pow(rim, 3.0) * 0.25; // soft rim light
        
        vec3 final_color = vec3(0.0) + (ambient + diff + rim) * vec3(0.5, 0.7, 1.0);
        FragColor = vec4(final_color, 1.0);
    } else {
        // Neon grid lines
        FragColor = vec4(u_color, 0.95);
    }
}
