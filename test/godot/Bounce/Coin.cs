using Godot;

// Picked up once: a freed node sends no more signals.
public partial class Coin : Area2D
{
    public override void _Ready()
    {
        BodyEntered += OnBodyEntered;
    }

    private void OnBodyEntered(Node2D body)
    {
        if (body is not Ball)
        {
            return;
        }
        GD.Print("coin taken by ", body.Name);
        QueueFree();
    }
}
